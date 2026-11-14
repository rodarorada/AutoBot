#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <vector>
#include <set>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;

struct MacroAction { int frame; bool hold; };

struct OrbChainNode {
    GameObject* orb;
    CCPoint entryPos;
    double entryYVel;
    bool entryUpside;
    int firstValidFrame;
    int lastValidFrame;
};

struct BotCheckpoint {
    int frame;
    size_t actionCount;
    bool allowHoldJump;
    bool deadEndPit;
    int singleClickFails;
    int totalDeaths;
    float posX;
    std::set<int> failedJumpFrames;
    std::set<int> failedDashReleaseFrames;
    std::vector<CCPoint> deadlyOrbs;
};

struct QuantumAgent {
    int id;
    int currentFrame;
    bool currentHold;
    int singleClickTimer;
    int humanDelayCounter;
    int humanTargetDelay;
    GameObject* lastClickedOrb;
    CCPoint savedPos;
    double savedYVel;
    bool savedUpsideDown;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
    std::vector<float> blacklistedCheckpointX;
    std::vector<OrbChainNode> orbChain;
    std::vector<CCPoint> idlePath;
    std::vector<CCPoint> holdPath;
    std::vector<CCPoint> oneClickPath;
};

namespace AutoBot {
    bool isRecording = false, isPlaying = false, showTrajectories = false;
    bool collectCoins = true, humanizeClicks = true, frameStepperActive = false;
    int maxAgents = 4, maxPath = 90, learningSpeed = 5;
    enumKeyCodes switchKey = KEY_Tab, stepKey = KEY_F;
    bool stepOneFrameRequested = false, isSimulating = false, botInput = false;
    bool isWaitingForKey = false, switchRequested = false;
    int activeAgentIndex = 0, nextAgentId = 1;
    std::vector<QuantumAgent> agents;

    void saveSettings() {
        auto mod = Mod::get();
        mod->setSavedValue("collectCoins", collectCoins);
        mod->setSavedValue("humanizeClicks", humanizeClicks);
        mod->setSavedValue("frameStepperActive", frameStepperActive);
        mod->setSavedValue("maxAgents", maxAgents);
        mod->setSavedValue("maxPath", maxPath);
        mod->setSavedValue("learningSpeed", learningSpeed);
        mod->setSavedValue("switchKey", static_cast<int>(switchKey));
    }

    void loadSettings() {
        auto mod = Mod::get();
        isRecording = false; isPlaying = false; showTrajectories = false;
        collectCoins = mod->getSavedValue<bool>("collectCoins", true);
        humanizeClicks = mod->getSavedValue<bool>("humanizeClicks", true);
        frameStepperActive = mod->getSavedValue<bool>("frameStepperActive", false);
        maxAgents = mod->getSavedValue<int>("maxAgents", 4);
        maxPath = mod->getSavedValue<int>("maxPath", 90);
        learningSpeed = mod->getSavedValue<int>("learningSpeed", 5);
        switchKey = static_cast<enumKeyCodes>(mod->getSavedValue<int>("switchKey", static_cast<int>(KEY_Tab)));
    }

    QuantumAgent* getActiveAgent() {
        if (agents.empty()) return nullptr;
        if (activeAgentIndex < 0 || activeAgentIndex >= static_cast<int>(agents.size())) activeAgentIndex = 0;
        return &agents[activeAgentIndex];
    }

    void cycleAgent() { if (agents.size() > 1) activeAgentIndex = (activeAgentIndex + 1) % static_cast<int>(agents.size()); }

    void saveMacro(const std::string& levelName, const std::vector<MacroAction>& acts) {
        auto path = Mod::get()->getSaveDir() / (levelName + ".txt");
        std::ofstream file(path.native());
        for (const auto& act : acts) file << act.frame << " " << act.hold << "\n";
    }
}

$on_mod(Loaded) { AutoBot::loadSettings(); }
class AutoBotPopup : public FLAlertLayer {
protected:
    CCLabelBMFont *m_agentsLabel = nullptr, *m_pathLabel = nullptr, *m_learnLabel = nullptr, *m_keyLabel = nullptr;

    bool init() override {
        if (!FLAlertLayer::init(150)) return false;
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({ 430.f, 315.f }); bg->setPosition(winSize / 2.f); m_mainLayer->addChild(bg);

        auto title = CCLabelBMFont::create("Quantum AutoBot Settings", "goldFont.fnt");
        title->setScale(0.6f); title->setPosition({ winSize.width / 2.f, winSize.height / 2.f + 138.f }); m_mainLayer->addChild(title);

        auto menu = CCMenu::create(); menu->setPosition({ 0.f, 0.f }); m_mainLayer->addChild(menu);
        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"); closeSpr->setScale(0.65f);
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(AutoBotPopup::onClose));
        closeBtn->setPosition({ winSize.width / 2.f - 198.f, winSize.height / 2.f + 142.f }); menu->addChild(closeBtn);

        auto addToggle = [&](const char* text, float y, bool state, SEL_MenuHandler cb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.34f); lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 185.f, winSize.height / 2.f + y }); m_mainLayer->addChild(lbl);
            auto offSpr = ButtonSprite::create("OFF", "goldFont.fnt", "GJ_button_06.png", 0.5f);
            auto onSpr = ButtonSprite::create("ON", "goldFont.fnt", "GJ_button_01.png", 0.5f);
            offSpr->setScale(0.56f); onSpr->setScale(0.56f);
            auto tog = CCMenuItemToggler::create(offSpr, onSpr, this, cb);
            tog->toggle(state); tog->setPosition({ winSize.width / 2.f + 155.f, winSize.height / 2.f + y }); menu->addChild(tog);
        };

        addToggle("Show NOP Paths:", 112.f, AutoBot::showTrajectories, menu_selector(AutoBotPopup::onTogglePaths));
        addToggle("Quantum Path Finder:", 85.f, AutoBot::isRecording, menu_selector(AutoBotPopup::onToggleRecord));
        addToggle("Playback (Showcase):", 58.f, AutoBot::isPlaying, menu_selector(AutoBotPopup::onTogglePlay));
        addToggle("Collect Coins:", 31.f, AutoBot::collectCoins, menu_selector(AutoBotPopup::onToggleCoins));
        addToggle("Humanize Clicks:", 4.f, AutoBot::humanizeClicks, menu_selector(AutoBotPopup::onToggleHuman));
        addToggle("Frame Stepper (Key F):", -23.f, AutoBot::frameStepperActive, menu_selector(AutoBotPopup::onToggleStepper));

        auto addStepper = [&](const char* text, float y, CCLabelBMFont*& valLbl, SEL_MenuHandler decCb, SEL_MenuHandler incCb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.34f); lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 185.f, winSize.height / 2.f + y }); m_mainLayer->addChild(lbl);
            auto minSpr = ButtonSprite::create("-", "goldFont.fnt", "GJ_button_04.png", 0.5f); minSpr->setScale(0.52f);
            auto minus = CCMenuItemSpriteExtra::create(minSpr, this, decCb);
            minus->setPosition({ winSize.width / 2.f + 115.f, winSize.height / 2.f + y }); menu->addChild(minus);
            valLbl = CCLabelBMFont::create("0", "goldFont.fnt"); valLbl->setScale(0.45f);
            valLbl->setPosition({ winSize.width / 2.f + 150.f, winSize.height / 2.f + y }); m_mainLayer->addChild(valLbl);
            auto plsSpr = ButtonSprite::create("+", "goldFont.fnt", "GJ_button_04.png", 0.5f); plsSpr->setScale(0.52f);
            auto plus = CCMenuItemSpriteExtra::create(plsSpr, this, incCb);
            plus->setPosition({ winSize.width / 2.f + 185.f, winSize.height / 2.f + y }); menu->addChild(plus);
        };

        addStepper("Learning Speed:", -52.f, m_learnLabel, menu_selector(AutoBotPopup::onDecLearn), menu_selector(AutoBotPopup::onIncLearn));
        addStepper("Max Path Length:", -79.f, m_pathLabel, menu_selector(AutoBotPopup::onDecPath), menu_selector(AutoBotPopup::onIncPath));
        addStepper("Max Quantum Agents:", -106.f, m_agentsLabel, menu_selector(AutoBotPopup::onDecAgents), menu_selector(AutoBotPopup::onIncAgents));

        auto bindLbl = CCLabelBMFont::create("Switch Agent Key:", "bigFont.fnt");
        bindLbl->setScale(0.34f); bindLbl->setAnchorPoint({ 0.f, 0.5f });
        bindLbl->setPosition({ winSize.width / 2.f - 185.f, winSize.height / 2.f - 134.f }); m_mainLayer->addChild(bindLbl);
        m_keyLabel = CCLabelBMFont::create("TAB", "goldFont.fnt"); m_keyLabel->setScale(0.45f);
        auto keyBtn = CCMenuItemSpriteExtra::create(m_keyLabel, this, menu_selector(AutoBotPopup::onBindKey));
        keyBtn->setPosition({ winSize.width / 2.f + 150.f, winSize.height / 2.f - 134.f }); menu->addChild(keyBtn);

        refreshLabels(); this->setKeypadEnabled(true); return true;
    }

    void refreshLabels() {
        if (m_learnLabel) m_learnLabel->setString(std::to_string(AutoBot::learningSpeed).c_str());
        if (m_pathLabel) m_pathLabel->setString(std::to_string(AutoBot::maxPath).c_str());
        if (m_agentsLabel) m_agentsLabel->setString(std::to_string(AutoBot::maxAgents).c_str());
        if (m_keyLabel) {
            if (AutoBot::isWaitingForKey) m_keyLabel->setString("[KEY]");
            else { const char* kn = CCKeyboardDispatcher::get()->keyToString(AutoBot::switchKey); m_keyLabel->setString(kn ? kn : "KEY"); }
        }
    }
    void keyDown(enumKeyCodes key, double timestamp) override {
        if (AutoBot::isWaitingForKey) {
            if (key != KEY_Escape) { AutoBot::switchKey = key; AutoBot::saveSettings(); }
            AutoBot::isWaitingForKey = false; refreshLabels(); return;
        }
        FLAlertLayer::keyDown(key, timestamp);
    }

    void onClose(CCObject*) { AutoBot::isWaitingForKey = false; AutoBot::saveSettings(); this->keyBackClicked(); }
    void onTogglePaths(CCObject* s) { AutoBot::showTrajectories = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }
    void onToggleCoins(CCObject* s) { AutoBot::collectCoins = !static_cast<CCMenuItemToggler*>(s)->isToggled(); AutoBot::saveSettings(); }
    void onToggleHuman(CCObject* s) { AutoBot::humanizeClicks = !static_cast<CCMenuItemToggler*>(s)->isToggled(); AutoBot::saveSettings(); }
    void onToggleStepper(CCObject* s) { AutoBot::frameStepperActive = !static_cast<CCMenuItemToggler*>(s)->isToggled(); AutoBot::saveSettings(); }
    void onDecLearn(CCObject*) { AutoBot::learningSpeed = std::max(1, AutoBot::learningSpeed - 1); AutoBot::saveSettings(); refreshLabels(); }
    void onIncLearn(CCObject*) { AutoBot::learningSpeed = std::min(15, AutoBot::learningSpeed + 1); AutoBot::saveSettings(); refreshLabels(); }
    void onDecAgents(CCObject*) { AutoBot::maxAgents = std::max(1, AutoBot::maxAgents - 1); AutoBot::saveSettings(); refreshLabels(); }
    void onIncAgents(CCObject*) { AutoBot::maxAgents = std::min(16, AutoBot::maxAgents + 1); AutoBot::saveSettings(); refreshLabels(); }
    void onDecPath(CCObject*) { AutoBot::maxPath = std::max(30, AutoBot::maxPath - 15); AutoBot::saveSettings(); refreshLabels(); }
    void onIncPath(CCObject*) { AutoBot::maxPath = std::min(240, AutoBot::maxPath + 15); AutoBot::saveSettings(); refreshLabels(); }
    void onBindKey(CCObject*) { AutoBot::isWaitingForKey = true; refreshLabels(); }

    void onToggleRecord(CCObject* s) {
        AutoBot::isRecording = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        if (auto pl = PlayLayer::get()) { AutoBot::botInput = true; pl->handleButton(false, 1, true); AutoBot::botInput = false; }
        if (AutoBot::isRecording) { AutoBot::isPlaying = false; AutoBot::agents.clear(); AutoBot::nextAgentId = 1; }
    }

    void onTogglePlay(CCObject* s) {
        AutoBot::isPlaying = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        if (AutoBot::isPlaying) AutoBot::isRecording = false;
    }

public:
    static AutoBotPopup* create() {
        auto ret = new AutoBotPopup();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret); return nullptr;
    }
};

class $modify(MyKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool arr, double timestamp) {
        if (down && !AutoBot::isWaitingForKey) {
            if (!arr && key == AutoBot::switchKey) AutoBot::switchRequested = true;
            if (key == AutoBot::stepKey && AutoBot::frameStepperActive) AutoBot::stepOneFrameRequested = true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, arr, timestamp);
    }
};

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create(); menu->setPosition({ winSize.width - 45.f, 45.f }); this->addChild(menu);
        auto spr = CircleButtonSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png", 1.f, CircleBaseColor::Green, CircleBaseSize::Small);
        menu->addChild(CCMenuItemSpriteExtra::create(spr, this, menu_selector(MyPauseLayer::onOpenAutoBot)));
    }
    void onOpenAutoBot(CCObject*) { AutoBotPopup::create()->show(); }
};

class $modify(MyEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remap) {
        if (AutoBot::isSimulating) return;
        EffectGameObject::triggerObject(layer, uniqueID, remap);
    }
};

class $modify(MyBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (AutoBot::isSimulating) return;
        if (!AutoBot::isRecording && !AutoBot::isPlaying) { GJBaseGameLayer::handleButton(down, button, isPlayer1); return; }
        if (AutoBot::botInput) GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        CCDrawNode* trajectoryNode = nullptr;
    };

    bool isAnyOrb(GameObject* obj) {
        if (!obj) return false;
        auto t = obj->getType(); int id = obj->m_objectID;
        return t == GameObjectType::YellowJumpRing || t == GameObjectType::PinkJumpRing ||
               t == GameObjectType::RedJumpRing || t == GameObjectType::GravityRing ||
               t == GameObjectType::GreenRing || t == GameObjectType::DashRing ||
               t == GameObjectType::GravityDashRing || id == 1330 || id == 1704 || id == 1751 || id == 3004;
    }

    void detectLevelBounds(CCPoint curPos, bool hasCeiling, float& outFloor, float& outCeil) {
        outFloor = 90.0f;
        outCeil = 390.0f;
        if (!hasCeiling || !this->m_objects) return;

        GameObject* bestPortal = nullptr;
        float bestDist = 999999.f;
        for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
            if (!obj) continue;
            int id = obj->m_objectID;
            bool isModePortal = (id == 13 || id == 47 || id == 111 || id == 660 || id == 745 || id == 1331 || id == 1933);
            if (isModePortal && obj->getPositionX() <= curPos.x + 20.f) {
                float d = curPos.x - obj->getPositionX();
                if (d < bestDist) { bestDist = d; bestPortal = obj; }
            }
        }
        if (bestPortal) {
            float snappedFloor = std::floor((bestPortal->getPositionY() - 150.0f) / 30.0f) * 30.0f;
            outFloor = std::max(90.0f, snappedFloor);
            outCeil = outFloor + 300.0f;
        }
    }

    bool applyOrbPhysics(double& yVel, bool& upside, float& gravDir, bool& isDashing, GameObject* obj) {
        auto t = obj->getType(); int id = obj->m_objectID;
        if (t == GameObjectType::YellowJumpRing) { yVel = 602.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::PinkJumpRing) { yVel = 435.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::RedJumpRing) { yVel = 830.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::GravityRing) {
            upside = !upside; gravDir = upside ? -1.0f : 1.0f; yVel = -220.0 * gravDir; isDashing = false; return true;
        }
        if (t == GameObjectType::GreenRing) {
            upside = !upside; gravDir = upside ? -1.0f : 1.0f; yVel = 580.0 * gravDir; isDashing = false; return true;
        }
        if (id == 1330) { yVel = -950.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::DashRing || id == 1704) { yVel = 0.0; isDashing = true; return true; }
        if (t == GameObjectType::GravityDashRing || id == 1751) {
            upside = !upside; gravDir = upside ? -1.0f : 1.0f; yVel = 0.0; isDashing = true; return true;
        }
        if (id == 3004) {
            upside = !upside; gravDir = upside ? -1.0f : 1.0f; yVel = -1500.0 * gravDir; isDashing = false; return true;
        }
        return false;
    }

    bool applyPassiveObject(double& yVel, bool& upside, float& gravDir, float& speedMult, bool& isDashing, bool& onGround, GameObject* obj) {
        auto t = obj->getType(); int id = obj->m_objectID;
        if (t == GameObjectType::YellowJumpPad || id == 35) { yVel = 860.0 * gravDir; onGround = false; isDashing = false; return true; }
        if (t == GameObjectType::PinkJumpPad || id == 140) { yVel = 610.0 * gravDir; onGround = false; isDashing = false; return true; }
        if (t == GameObjectType::RedJumpPad || id == 1332) { yVel = 1150.0 * gravDir; onGround = false; isDashing = false; return true; }
        if (t == GameObjectType::GravityPad || id == 67) {
            upside = !upside; gravDir = upside ? -1.0f : 1.0f; yVel = -300.0 * gravDir; onGround = false; isDashing = false; return true;
        }
        if (id == 3005) { upside = !upside; gravDir = upside ? -1.0f : 1.0f; yVel = -1600.0 * gravDir; onGround = false; isDashing = false; return true; }
        if (id == 200) { speedMult = 0.807f; return true; }
        if (id == 201) { speedMult = 1.0f; return true; }
        if (id == 202) { speedMult = 1.243f; return true; }
        if (id == 203) { speedMult = 1.502f; return true; }
        if (id == 1334) { speedMult = 1.848f; return true; }
        if (id == 1817) { isDashing = false; return true; }
        return false;
    }
    int runNOP(CCPoint startPos, double startYVel, bool startUpside, bool startGrounded, int mode, int steps, std::vector<CCPoint>& pts, bool& outDied, bool& outGotCoin, bool& outTouchedFloor) {
        pts.clear(); outDied = false; outGotCoin = false; outTouchedFloor = false;
        PlayerObject* realPlayer = this->m_player1;
        if (!realPlayer) return 0;

        CCPoint pos = startPos;
        double yVel = startYVel;
        bool upside = startUpside;
        bool onGround = startGrounded;
        bool isDashing = realPlayer->m_isDashing;
        float mult = (realPlayer->m_playerSpeed > 0.1f) ? realPlayer->m_playerSpeed : 1.0f;
        float scale = realPlayer->getScale();
        float halfW = 15.0f * scale;
        float halfH = 15.0f * scale;

        bool isShip = realPlayer->m_isShip, isBall = realPlayer->m_isBall, isBird = realPlayer->m_isBird;
        bool isDart = realPlayer->m_isDart, isRobot = realPlayer->m_isRobot, isSpider = realPlayer->m_isSpider;
        bool isSwing = realPlayer->m_isSwing;
        bool hasCeiling = isShip || isBall || isBird || isDart || isSpider || isSwing;

        float floorY = 90.0f, ceilY = 390.0f;
        detectLevelBounds(startPos, hasCeiling, floorY, ceilY);

        int survived = 0; bool jumpedOnce = false;
        GameObject* lastUsedObj = nullptr;
        const float subDt = (1.0f / 60.0f) / 4.0f;

        for (int s = 0; s < steps; ++s) {
            bool hold = (mode == 1) || (mode == 2 && !jumpedOnce);
            bool died = false;

            for (int sub = 0; sub < 4; ++sub) {
                CCPoint prevPos = pos;
                float gravDir = upside ? -1.0f : 1.0f;
                if (yVel * gravDir > 25.0) onGround = false;

                if (isDashing && hold) { yVel = 0.0; }
                else {
                    isDashing = false;
                    if (isDart) yVel = (hold ? 311.58f * mult : -311.58f * mult) * gravDir;
                    else if (isShip) {
                        yVel += (hold ? 1380.0f : -1120.0f) * gravDir * subDt;
                        yVel = std::clamp(yVel, -550.0, 550.0);
                    } else if (isSwing) {
                        if (hold && s == 0 && sub == 0) { upside = !upside; gravDir = -gravDir; jumpedOnce = true; }
                        yVel = std::clamp(yVel - 1800.0 * gravDir * subDt, -520.0, 520.0);
                    } else if (isBird) {
                        if (hold && s == 0 && sub == 0) { yVel = 430.0 * gravDir; jumpedOnce = true; }
                        yVel -= 1950.0 * gravDir * subDt;
                    } else {
                        if (hold && onGround) {
                            if (isBall || isSpider) {
                                upside = !upside; gravDir = upside ? -1.0f : 1.0f;
                                yVel = (isSpider ? -1200.0 : -200.0) * gravDir;
                            } else yVel = (isRobot ? 640.0 : 602.0) * gravDir;
                            onGround = false; jumpedOnce = true;
                        }
                        yVel = std::clamp(yVel - 2865.0 * gravDir * subDt, -810.0, 810.0);
                    }
                }

                pos.x += 311.58f * mult * subDt;
                pos.y += static_cast<float>(yVel) * subDt;
                onGround = false;

                if (pos.y - halfH <= floorY) {
                    pos.y = floorY + halfH;
                    if (yVel < 0) yVel = 0;
                    if (!upside) { onGround = true; outTouchedFloor = true; }
                }
                if (hasCeiling && pos.y + halfH >= ceilY) {
                    pos.y = ceilY - halfH;
                    if (yVel > 0) yVel = 0;
                    if (upside) { onGround = true; outTouchedFloor = true; }
                }

                if (this->m_objects) {
                    CCRect pRect = { pos.x - halfW, pos.y - halfH, halfW * 2.f, halfH * 2.f };
                    CCRect killRect = { pos.x - halfW * 0.45f, pos.y - halfH * 0.45f, halfW * 0.9f, halfH * 0.9f };
                    bool hasDBlock = false, hasHBlock = false;

                    for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
                        if (!obj || std::abs(obj->getPositionX() - pos.x) > 50.f) continue;
                        if (obj->m_objectID == 1816 && pRect.intersectsRect(obj->getObjectRect())) hasDBlock = true;
                        if (obj->m_objectID == 1818 && pRect.intersectsRect(obj->getObjectRect())) hasHBlock = true;
                    }

                    for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
                        if (!obj || std::abs(obj->getPositionX() - pos.x) > 50.f) continue;
                        auto t = obj->getType();
                        CCRect oRect = obj->getObjectRect();
                        if (oRect.size.width <= 0 || oRect.size.height <= 0) continue;

                        if (t == GameObjectType::Hazard && killRect.intersectsRect(oRect)) { died = true; break; }
                        if ((t == GameObjectType::SecretCoin || t == GameObjectType::UserCoin) && pRect.intersectsRect(oRect)) outGotCoin = true;

                        if (obj != lastUsedObj && pRect.intersectsRect(oRect)) {
                            if (applyPassiveObject(yVel, upside, gravDir, mult, isDashing, onGround, obj)) lastUsedObj = obj;
                            else if ((mode == 1 || mode == 2) && applyOrbPhysics(yVel, upside, gravDir, isDashing, obj)) {
                                lastUsedObj = obj; jumpedOnce = true; onGround = false;
                            }
                        }

                        if (t == GameObjectType::Solid && pRect.intersectsRect(oRect)) {
                            float prevBottom = prevPos.y - halfH, prevTop = prevPos.y + halfH;
                            float objTop = oRect.getMaxY(), objBottom = oRect.getMinY();
                            if (prevBottom >= objTop - 8.0f && yVel <= 0) {
                                pos.y = objTop + halfH; yVel = 0; if (!upside) onGround = true;
                            } else if (prevTop <= objBottom + 8.0f && yVel >= 0) {
                                if (isShip || isBird || isBall || isSpider || isSwing || hasHBlock || (isDart && hasDBlock)) {
                                    pos.y = objBottom - halfH; yVel = 0; if (upside) onGround = true;
                                } else { died = true; break; }
                            } else if (!hasDBlock) { died = true; break; }
                        }
                    }
                }
                if (died) break;
            }
            pts.push_back(pos);
            if (died) { outDied = true; break; }
            survived++;
        }
        return survived;
    }
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        m_fields->trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(m_fields->trajectoryNode, 1000);
        return true;
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        if (m_fields->trajectoryNode) m_fields->trajectoryNode->clear();
        if (!this->m_player1) return;
        if (AutoBot::agents.empty()) AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, 0, 0, 1 + (rand() % 3), nullptr, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, {}, {}, {} });
        if (auto* ag = AutoBot::getActiveAgent()) {
            ag->singleClickTimer = 0; ag->humanDelayCounter = 0; ag->humanTargetDelay = 1 + (rand() % 3); ag->lastClickedOrb = nullptr; ag->orbChain.clear();
            if (AutoBot::isRecording && !ag->checkpoints.empty()) {
                ag->currentFrame = ag->checkpoints.back().frame;
                ag->actions.resize(ag->checkpoints.back().actionCount);
            } else { ag->currentFrame = 0; ag->actions.clear(); }
        }
    }

    void update(float dt) override {
        if (AutoBot::frameStepperActive) {
            if (!AutoBot::stepOneFrameRequested) return;
            AutoBot::stepOneFrameRequested = false; dt = 1.0f / 60.0f;
        }
        PlayLayer::update(dt);
    }

    void postUpdate(float dt) override {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1 || this->m_player1->m_isDead || AutoBot::isSimulating) return;

        if (AutoBot::switchRequested) {
            AutoBot::switchRequested = false; AutoBot::cycleAgent();
            if (auto* ag = AutoBot::getActiveAgent()) {
                this->m_player1->m_position = ag->savedPos; this->m_player1->setPosition(ag->savedPos);
                this->m_player1->m_yVelocity = ag->savedYVel; this->m_player1->m_isUpsideDown = ag->savedUpsideDown;
            }
        }

        if (m_fields->trajectoryNode) m_fields->trajectoryNode->clear();
        if (!AutoBot::showTrajectories && !AutoBot::isRecording) return;
        if (AutoBot::agents.empty()) AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, 0, 0, 1 + (rand() % 3), nullptr, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, {}, {}, {} });

        auto* ag = AutoBot::getActiveAgent();
        auto p = this->m_player1; CCPoint curPos = p->getPosition();
        bool isHoldOnlyMode = p->m_isShip || p->m_isDart;
        bool isClickOnlyMode = p->m_isBird || p->m_isBall || p->m_isSpider || p->m_isSwing;

        GameObject* currentOrb = nullptr; bool onJBlock = false;
        if (this->m_objects) {
            CCRect pRect = { curPos.x - 15.f, curPos.y - 15.f, 30.f, 30.f };
            for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
                if (!obj || std::abs(obj->getPositionX() - curPos.x) > 45.f) continue;
                if (obj->m_objectID == 1815 && pRect.intersectsRect(obj->getObjectRect())) onJBlock = true;
                if (isAnyOrb(obj) && pRect.intersectsRect(obj->getObjectRect())) currentOrb = obj;
            }
        }

        bool diedIdle = false, diedHold = false, diedOne = false;
        bool coinIdle = false, coinHold = false, coinOne = false;
        bool floorIdle = false, floorHold = false, floorOne = false;

        int sIdle = runNOP(curPos, p->m_yVelocity, p->m_isUpsideDown, p->m_isOnGround, 0, AutoBot::maxPath, ag->idlePath, diedIdle, coinIdle, floorIdle);

        CCPoint jumpOrigin = curPos; double jumpYVel = p->m_yVelocity; bool jumpGround = p->m_isOnGround;
        bool canActNow = p->m_isOnGround || (currentOrb != nullptr) || isHoldOnlyMode || isClickOnlyMode;
        if (!canActNow && ag->idlePath.size() > 2) {
            for (size_t i = 2; i < ag->idlePath.size(); ++i) {
                if (std::abs(ag->idlePath[i].y - ag->idlePath[i - 1].y) < 0.2f && ag->idlePath[i].x > curPos.x + 2.f) {
                    jumpOrigin = ag->idlePath[i]; jumpYVel = 0.0; jumpGround = true; canActNow = true; break;
                }
            }
        }

        ag->holdPath.clear(); ag->oneClickPath.clear(); int sHold = 0, sOne = 0;
        if (canActNow) {
            if (!isClickOnlyMode) sHold = runNOP(jumpOrigin, jumpYVel, p->m_isUpsideDown, jumpGround, 1, AutoBot::maxPath, ag->holdPath, diedHold, coinHold, floorHold);
            if (!isHoldOnlyMode) sOne = runNOP(jumpOrigin, jumpYVel, p->m_isUpsideDown, jumpGround, 2, AutoBot::maxPath, ag->oneClickPath, diedOne, coinOne, floorOne);
        }
        if (AutoBot::isRecording) {
            if (!this->m_isPracticeMode) this->togglePracticeMode(true);
            unsigned int cpCount = this->m_checkpointArray ? this->m_checkpointArray->count() : 0;

            if (cpCount > ag->checkpoints.size() && cpCount > 0) {
                bool isBlacklisted = false;
                for (float bx : ag->blacklistedCheckpointX) if (std::abs(bx - curPos.x) < 120.f) isBlacklisted = true;
                if (isBlacklisted) { this->removeCheckpoint(false); cpCount--; }
            }

            while (ag->checkpoints.size() < cpCount) ag->checkpoints.push_back({ ag->currentFrame, ag->actions.size(), false, false, 0, 0, curPos.x, {}, {}, {} });
            while (ag->checkpoints.size() > cpCount && !ag->checkpoints.empty()) ag->checkpoints.pop_back();

            ag->currentFrame++; ag->savedPos = curPos; ag->savedYVel = p->m_yVelocity; ag->savedUpsideDown = p->m_isUpsideDown;
            bool wantHold = false;
            bool holdAllowed = isHoldOnlyMode || (!ag->checkpoints.empty() && ag->checkpoints.back().allowHoldJump);
            bool forbidden = (!ag->checkpoints.empty() && ag->checkpoints.back().failedJumpFrames.count(ag->currentFrame));
            bool dashReleaseForbidden = (!ag->checkpoints.empty() && ag->checkpoints.back().failedDashReleaseFrames.count(ag->currentFrame));

            bool isDashOrb = currentOrb && (currentOrb->getType() == GameObjectType::DashRing || currentOrb->getType() == GameObjectType::GravityDashRing || currentOrb->m_objectID == 1704 || currentOrb->m_objectID == 1751);

            float idleDeathX = ag->idlePath.empty() ? curPos.x : ag->idlePath.back().x;
            float idleEndY = ag->idlePath.empty() ? curPos.y : ag->idlePath.back().y;
            float oneEndX = ag->oneClickPath.empty() ? curPos.x : ag->oneClickPath.back().x;
            float holdEndX = ag->holdPath.empty() ? curPos.x : ag->holdPath.back().x;

            bool safeOne = !isHoldOnlyMode && !diedOne && !ag->oneClickPath.empty();
            bool safeHold = holdAllowed && !isClickOnlyMode && !diedHold && !ag->holdPath.empty() && !onJBlock;
            bool isFalling = (idleEndY < curPos.y - 5.0f);

            // Если падаем в яму, но НЕТ ни одной платформы для приземления (!safeOne && !safeHold) — помечаем чекпоинт как тупиковую яму
            if (diedIdle && isFalling && !floorIdle && !safeOne && !safeHold && !ag->checkpoints.empty()) {
                ag->checkpoints.back().deadEndPit = true;
            }

            if (p->m_isDashing) {
                wantHold = (diedIdle || sIdle < 15 || dashReleaseForbidden);
            } else if (ag->singleClickTimer > 0) {
                ag->singleClickTimer--; wantHold = true;
            } else if (currentOrb && currentOrb != ag->lastClickedOrb) {
                bool orbDeadly = false;
                if (!ag->checkpoints.empty()) { for (auto& op : ag->checkpoints.back().deadlyOrbs) if (op.getDistance(currentOrb->getPosition()) < 15.f) orbDeadly = true; }
                if (!orbDeadly) {
                    ag->lastClickedOrb = currentOrb; ag->singleClickTimer = isDashOrb ? 3 : 1; wantHold = true;
                    ag->orbChain.push_back({ currentOrb, curPos, p->m_yVelocity, p->m_isUpsideDown, ag->currentFrame, ag->currentFrame + 4 });
                }
            } else if (!forbidden && canActNow && (p->m_isOnGround || isHoldOnlyMode || isClickOnlyMode)) {
                bool isPit = diedIdle && isFalling && (!floorIdle || (safeOne || safeHold));
                bool shouldJumpOne = safeOne && (isPit || oneEndX > idleDeathX + 5.0f);
                bool shouldJumpHold = safeHold && (isPit || holdEndX > idleDeathX + 5.0f);

                if (diedIdle) {
                    if (shouldJumpOne) {
                        if (AutoBot::humanizeClicks && ag->humanDelayCounter < ag->humanTargetDelay && p->m_isOnGround && sIdle > 3) {
                            ag->humanDelayCounter++;
                        } else {
                            ag->humanDelayCounter = 0; ag->humanTargetDelay = 1 + (rand() % 4);
                            ag->singleClickTimer = AutoBot::humanizeClicks ? (1 + (rand() % 2)) : 1; wantHold = true;
                        }
                    } else if (shouldJumpHold) wantHold = true;
                } else if (AutoBot::collectCoins && !coinIdle && ((coinOne && safeOne) || (coinHold && safeHold))) {
                    if (coinOne && safeOne) { ag->singleClickTimer = 1; wantHold = true; } else if (safeHold) wantHold = true;
                } else ag->humanDelayCounter = 0;
            }

            if (ag->currentHold != wantHold) {
                ag->currentHold = wantHold; ag->actions.push_back({ ag->currentFrame, wantHold });
                AutoBot::botInput = true; this->handleButton(wantHold, 1, true); AutoBot::botInput = false;
            }
        }

        if (AutoBot::showTrajectories && m_fields->trajectoryNode) {
            auto draw = [&](CCPoint start, const std::vector<CCPoint>& pts, ccColor4F col, bool died) {
                if (pts.empty()) return;
                CCPoint prev = start;
                for (const auto& pt : pts) { m_fields->trajectoryNode->drawSegment(prev, pt, 0.55f, col); prev = pt; }
                float hb = 15.0f * p->getScale(); CCPoint end = pts.back();
                ccColor4F box = died ? ccColor4F{ 1.f, 0.f, 0.f, 0.95f } : col;
                m_fields->trajectoryNode->drawRect({ end.x - hb, end.y - hb }, { end.x + hb, end.y + hb }, { 0, 0, 0, 0 }, 0.5f, box);
            };
            draw(curPos, ag->idlePath, { 0.1f, 1.0f, 0.1f, 0.9f }, diedIdle);
            if (!isClickOnlyMode) draw(jumpOrigin, ag->holdPath, { 1.0f, 0.12f, 0.12f, 0.9f }, diedHold);
            if (!isHoldOnlyMode) draw(jumpOrigin, ag->oneClickPath, { 1.0f, 0.65f, 0.0f, 0.98f }, diedOne);
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) override {
        if (AutoBot::isSimulating) { if (player) player->m_isDead = true; return; }
        if (AutoBot::isRecording) {
            if (auto* ag = AutoBot::getActiveAgent()) {
                if (!ag->checkpoints.empty()) {
                    auto& cp = ag->checkpoints.back();
                    cp.singleClickFails++; cp.totalDeaths++;
                    if (cp.singleClickFails >= 3) cp.allowHoldJump = true;

                    // Для тупиковой ямы без платформ откатываемся уже через 3 смерти, для обычных мест — через 50
                    int deathLimit = cp.deadEndPit ? 3 : 50;
                    if (cp.totalDeaths >= deathLimit && this->m_checkpointArray && this->m_checkpointArray->count() > 0) {
                        ag->blacklistedCheckpointX.push_back(cp.posX);
                        this->removeCheckpoint(false);
                        ag->checkpoints.pop_back();
                    } else {
                        for (auto it = ag->actions.rbegin(); it != ag->actions.rend(); ++it) {
                            if (it->hold) { for (int k = 0; k < AutoBot::learningSpeed; ++k) cp.failedJumpFrames.insert(it->frame + k); break; }
                            else { for (int k = 0; k < AutoBot::learningSpeed * 2; ++k) cp.failedDashReleaseFrames.insert(it->frame + k); }
                        }
                    }
                }
            }
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (AutoBot::isRecording) { AutoBot::isRecording = false; if (auto* ag = AutoBot::getActiveAgent()) AutoBot::saveMacro(this->m_level->m_levelName, ag->actions); }
    }
};