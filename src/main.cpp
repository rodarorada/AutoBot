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
    int singleClickFails;
    std::set<int> failedJumpFrames;
    std::vector<float> triedEdges;
    std::vector<CCPoint> deadlyOrbs;
};

struct QuantumAgent {
    int id;
    int currentFrame;
    bool currentHold;
    int singleClickTimer;
    int humanDelayCounter;
    GameObject* lastClickedOrb;
    CCPoint savedPos;
    double savedYVel;
    bool savedUpsideDown;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
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
        bg->setContentSize({ 430.f, 315.f });
        bg->setPosition(winSize / 2.f);
        m_mainLayer->addChild(bg);

        auto title = CCLabelBMFont::create("Quantum AutoBot Settings", "goldFont.fnt");
        title->setScale(0.6f);
        title->setPosition({ winSize.width / 2.f, winSize.height / 2.f + 138.f });
        m_mainLayer->addChild(title);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(menu);

        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.65f);
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(AutoBotPopup::onClose));
        closeBtn->setPosition({ winSize.width / 2.f - 198.f, winSize.height / 2.f + 142.f });
        menu->addChild(closeBtn);

        auto addToggle = [&](const char* text, float y, bool state, SEL_MenuHandler cb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.34f); lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 185.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(lbl);
            auto offSpr = ButtonSprite::create("OFF", "goldFont.fnt", "GJ_button_06.png", 0.5f);
            auto onSpr = ButtonSprite::create("ON", "goldFont.fnt", "GJ_button_01.png", 0.5f);
            offSpr->setScale(0.56f); onSpr->setScale(0.56f);
            auto tog = CCMenuItemToggler::create(offSpr, onSpr, this, cb);
            tog->toggle(state); tog->setPosition({ winSize.width / 2.f + 155.f, winSize.height / 2.f + y });
            menu->addChild(tog);
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
            lbl->setPosition({ winSize.width / 2.f - 185.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(lbl);
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

class $modify(MyPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (AutoBot::isSimulating) {
            if (this->m_regularTrail) this->m_regularTrail->setVisible(false);
            if (this->m_playerGroundParticles) this->m_playerGroundParticles->stopSystem();
            if (this->m_trailingParticles) this->m_trailingParticles->stopSystem();
            if (this->m_shipClickParticles) this->m_shipClickParticles->stopSystem();
            if (this->m_landParticles0) this->m_landParticles0->stopSystem();
            if (this->m_landParticles1) this->m_landParticles1->stopSystem();
        }
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
        PlayerObject* nopSim = nullptr;
    };

    void rebuildNOP() {
        if (m_fields->nopSim) { m_fields->nopSim->removeFromParent(); m_fields->nopSim = nullptr; }
        m_fields->nopSim = PlayerObject::create(1, 1, this, this->m_objectLayer, true);
        if (m_fields->nopSim) { m_fields->nopSim->setVisible(false); this->m_objectLayer->addChild(m_fields->nopSim); }
    }

    bool isAnyOrb(GameObject* obj) {
        if (!obj) return false;
        auto t = obj->getType(); int id = obj->m_objectID;
        return t == GameObjectType::YellowJumpRing || t == GameObjectType::PinkJumpRing ||
               t == GameObjectType::RedJumpRing || t == GameObjectType::GravityRing ||
               t == GameObjectType::GreenRing || t == GameObjectType::DashRing ||
               t == GameObjectType::GravityDashRing || id == 1330 || id == 1704 || id == 1751 || id == 3004;
    }

    bool applyOrbPhysics(PlayerObject* nop, GameObject* obj, float& gravDir, bool& isDashing) {
        auto t = obj->getType(); int id = obj->m_objectID;
        if (t == GameObjectType::YellowJumpRing) { nop->m_yVelocity = 602.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::PinkJumpRing) { nop->m_yVelocity = 435.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::RedJumpRing) { nop->m_yVelocity = 830.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::GravityRing) {
            nop->m_isUpsideDown = !nop->m_isUpsideDown; gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
            nop->m_yVelocity = -220.0 * gravDir; isDashing = false; return true;
        }
        if (t == GameObjectType::GreenRing) {
            nop->m_isUpsideDown = !nop->m_isUpsideDown; gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
            nop->m_yVelocity = 580.0 * gravDir; isDashing = false; return true;
        }
        if (id == 1330) { nop->m_yVelocity = -950.0 * gravDir; isDashing = false; return true; }
        if (t == GameObjectType::DashRing || id == 1704) { nop->m_yVelocity = 0.0; isDashing = true; return true; }
        if (t == GameObjectType::GravityDashRing || id == 1751) {
            nop->m_isUpsideDown = !nop->m_isUpsideDown; gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
            nop->m_yVelocity = 0.0; isDashing = true; return true;
        }
        if (id == 3004) {
            nop->m_isUpsideDown = !nop->m_isUpsideDown; gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
            nop->m_yVelocity = -1500.0 * gravDir; isDashing = false; return true;
        }
        return false;
    }

    int runNOP(CCPoint startPos, double startYVel, bool startUpside, bool startGrounded, int mode, int steps, std::vector<CCPoint>& pts, bool& outDied, bool& outGotCoin) {
        pts.clear(); outDied = false; outGotCoin = false;
        PlayerObject* realPlayer = this->m_player1;
        if (!m_fields->nopSim) rebuildNOP();
        PlayerObject* nop = m_fields->nopSim;
        if (!nop || !realPlayer) return 0;

        nop->m_isDead = false; nop->m_isDashing = false; nop->m_isLocked = false;
        nop->m_position = startPos; nop->m_lastPosition = startPos; nop->setPosition(startPos);
        nop->m_yVelocity = startYVel; nop->m_gravity = realPlayer->m_gravity;
        nop->m_playerSpeed = (realPlayer->m_playerSpeed > 0.1f) ? realPlayer->m_playerSpeed : 1.0f;
        nop->m_isOnGround = startGrounded; nop->m_isUpsideDown = startUpside;
        nop->m_isShip = realPlayer->m_isShip; nop->m_isBall = realPlayer->m_isBall;
        nop->m_isBird = realPlayer->m_isBird; nop->m_isDart = realPlayer->m_isDart;
        nop->m_isRobot = realPlayer->m_isRobot; nop->m_isSpider = realPlayer->m_isSpider;
        nop->m_isSwing = realPlayer->m_isSwing;
        nop->m_vehicleSize = realPlayer->m_vehicleSize;
        nop->setScale(realPlayer->getScale());

        bool isDashing = realPlayer->m_isDashing;
        AutoBot::isSimulating = true;
        this->m_player1 = nop;
        int survived = 0; bool jumpedOnce = false;
        GameObject* lastUsedOrb = nullptr;
        const float subDt = (1.0f / 60.0f) / 4.0f;
        float mult = nop->m_playerSpeed;

        for (int s = 0; s < steps; ++s) {
            bool hold = (mode == 1) || (mode == 2 && !jumpedOnce);
            for (int sub = 0; sub < 4; ++sub) {
                nop->m_lastPosition = nop->m_position;
                float gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
                if (nop->m_yVelocity * gravDir > 15.0) nop->m_isOnGround = false;

                if (isDashing && hold) { nop->m_yVelocity = 0.0; }
                else {
                    isDashing = false;
                    if (nop->m_isDart) nop->m_yVelocity = (hold ? 311.58f * mult : -311.58f * mult) * gravDir;
                    else if (nop->m_isShip) {
                        nop->m_yVelocity += (hold ? 1380.0f : -1120.0f) * gravDir * subDt;
                        nop->m_yVelocity = std::clamp(nop->m_yVelocity, -550.0, 550.0);
                    } else if (nop->m_isSwing) {
                        if (hold && s == 0 && sub == 0) { nop->m_isUpsideDown = !nop->m_isUpsideDown; gravDir = -gravDir; jumpedOnce = true; }
                        nop->m_yVelocity = std::clamp(nop->m_yVelocity - 1800.0 * gravDir * subDt, -520.0, 520.0);
                    } else if (nop->m_isBird) {
                        if (hold && s == 0 && sub == 0) { nop->m_yVelocity = 430.0 * gravDir; jumpedOnce = true; }
                        nop->m_yVelocity -= 1950.0 * gravDir * subDt;
                    } else {
                        if (hold && nop->m_isOnGround) {
                            if (nop->m_isBall || nop->m_isSpider) {
                                nop->m_isUpsideDown = !nop->m_isUpsideDown; gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
                                nop->m_yVelocity = (nop->m_isSpider ? -1200.0 : -200.0) * gravDir;
                            } else nop->m_yVelocity = (nop->m_isRobot ? 640.0 : 602.0) * gravDir;
                            nop->m_isOnGround = false; jumpedOnce = true;
                        }
                        nop->m_yVelocity = std::clamp(nop->m_yVelocity - 2865.0 * gravDir * subDt, -810.0, 810.0);
                    }
                }
                nop->m_position.x += 311.58f * mult * subDt;
                nop->m_position.y += static_cast<float>(nop->m_yVelocity) * subDt;
                if (nop->m_position.y < 0.0f) {
                    nop->m_position.y = 0.0f;
                    if (nop->m_yVelocity < 0) nop->m_yVelocity = 0;
                    nop->m_isOnGround = true;
                }
                nop->setPosition(nop->m_position);
                this->checkCollisions(nop, subDt, true);

                if (!nop->m_isDead && this->m_objects) {
                    CCRect pRect = { nop->m_position.x - 11.f, nop->m_position.y - 11.f, 22.f, 22.f };
                    for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
                        if (!obj || std::abs(obj->getPositionX() - nop->m_position.x) > 45.f) continue;
                        auto t = obj->getType();
                        if (t == GameObjectType::Hazard && pRect.intersectsRect(obj->getObjectRect())) { nop->m_isDead = true; break; }
                        if ((t == GameObjectType::SecretCoin || t == GameObjectType::UserCoin) && pRect.intersectsRect(obj->getObjectRect())) outGotCoin = true;
                        if ((mode == 1 || mode == 2) && obj != lastUsedOrb && pRect.intersectsRect(obj->getObjectRect())) {
                            if (applyOrbPhysics(nop, obj, gravDir, isDashing)) { lastUsedOrb = obj; jumpedOnce = true; nop->m_isOnGround = false; }
                        }
                    }
                }
                if (nop->m_isDead) break;
            }
            pts.push_back(nop->getPosition());
            if (nop->m_isDead) { outDied = true; break; }
            survived++;
            if (mode == 2 && jumpedOnce && nop->m_isOnGround) break;
        }
        this->m_player1 = realPlayer; AutoBot::isSimulating = false;
        return survived;
    }

    bool isOrbAlwaysFatal(const OrbChainNode& node) {
        std::vector<CCPoint> tmp; bool died = false, coin = false;
        int span = std::max(1, node.lastValidFrame - node.firstValidFrame);
        float mult = (this->m_player1 && this->m_player1->m_playerSpeed > 0.1f) ? this->m_player1->m_playerSpeed : 1.0f;
        float dxPerFrame = 311.58f * mult * (1.0f / 60.0f);
        for (int offset : { 0, span, span / 2 }) {
            CCPoint testPos = { node.entryPos.x + offset * dxPerFrame, node.entryPos.y };
            runNOP(testPos, node.entryYVel, node.entryUpside, false, 2, AutoBot::maxPath, tmp, died, coin);
            if (!died) return false;
        }
        return true;
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        m_fields->trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(m_fields->trajectoryNode, 1000);
        rebuildNOP(); return true;
    }

    void resetLevel() override {
        PlayLayer::resetLevel();
        if (m_fields->trajectoryNode) m_fields->trajectoryNode->clear();
        rebuildNOP();
        if (!this->m_player1) return;
        if (AutoBot::agents.empty()) AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, 0, 0, nullptr, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, {}, {} });
        if (auto* ag = AutoBot::getActiveAgent()) {
            ag->singleClickTimer = 0; ag->humanDelayCounter = 0; ag->lastClickedOrb = nullptr; ag->orbChain.clear();
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
        if (AutoBot::agents.empty()) AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, 0, 0, nullptr, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, {}, {} });

        auto* ag = AutoBot::getActiveAgent();
        auto p = this->m_player1; CCPoint curPos = p->getPosition();
        bool isHoldOnlyMode = p->m_isShip || p->m_isDart;
        bool isClickOnlyMode = p->m_isBird || p->m_isBall || p->m_isSpider || p->m_isSwing;

        GameObject* currentOrb = nullptr;
        if (this->m_objects) {
            CCRect pRect = { curPos.x - 15.f, curPos.y - 15.f, 30.f, 30.f };
            for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
                if (!obj || std::abs(obj->getPositionX() - curPos.x) > 45.f) continue;
                if (isAnyOrb(obj) && pRect.intersectsRect(obj->getObjectRect())) { currentOrb = obj; break; }
            }
        }

        bool diedIdle = false, diedHold = false, diedOne = false, coinIdle = false, coinHold = false, coinOne = false;
        int sIdle = runNOP(curPos, p->m_yVelocity, p->m_isUpsideDown, p->m_isOnGround, 0, AutoBot::maxPath, ag->idlePath, diedIdle, coinIdle);
        if (sIdle < 10 && !diedIdle) { rebuildNOP(); sIdle = runNOP(curPos, p->m_yVelocity, p->m_isUpsideDown, p->m_isOnGround, 0, AutoBot::maxPath, ag->idlePath, diedIdle, coinIdle); }

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
            if (!isClickOnlyMode) {
                sHold = runNOP(jumpOrigin, jumpYVel, p->m_isUpsideDown, jumpGround, 1, AutoBot::maxPath, ag->holdPath, diedHold, coinHold);
                if (sHold < 10 && !diedHold) { rebuildNOP(); sHold = runNOP(jumpOrigin, jumpYVel, p->m_isUpsideDown, jumpGround, 1, AutoBot::maxPath, ag->holdPath, diedHold, coinHold); }
            }
            if (!isHoldOnlyMode) {
                sOne = runNOP(jumpOrigin, jumpYVel, p->m_isUpsideDown, jumpGround, 2, AutoBot::maxPath, ag->oneClickPath, diedOne, coinOne);
                if (sOne < 8 && !diedOne) { rebuildNOP(); sOne = runNOP(jumpOrigin, jumpYVel, p->m_isUpsideDown, jumpGround, 2, AutoBot::maxPath, ag->oneClickPath, diedOne, coinOne); }
            }
        }

        if (AutoBot::isRecording) {
            if (!this->m_isPracticeMode) this->togglePracticeMode(true);
            unsigned int cpCount = this->m_checkpointArray ? this->m_checkpointArray->count() : 0;
            while (ag->checkpoints.size() < cpCount) ag->checkpoints.push_back({ ag->currentFrame, ag->actions.size(), false, 0, {}, {}, {} });
            while (ag->checkpoints.size() > cpCount && !ag->checkpoints.empty()) ag->checkpoints.pop_back();

            ag->currentFrame++; ag->savedPos = curPos; ag->savedYVel = p->m_yVelocity; ag->savedUpsideDown = p->m_isUpsideDown;
            bool wantHold = false;
            bool holdAllowed = isHoldOnlyMode || (!ag->checkpoints.empty() && ag->checkpoints.back().allowHoldJump);
            bool forbidden = (!ag->checkpoints.empty() && ag->checkpoints.back().failedJumpFrames.count(ag->currentFrame));

            bool isBlockEdge = p->m_isOnGround && !ag->idlePath.empty() && (ag->idlePath.back().y < curPos.y - 18.f) && !diedIdle;
            if (isBlockEdge && !ag->checkpoints.empty()) {
                bool edgeTried = false;
                for (float ex : ag->checkpoints.back().triedEdges) if (std::abs(ex - curPos.x) < 25.f) edgeTried = true;
                if (!edgeTried) { ag->checkpoints.back().triedEdges.push_back(curPos.x); forbidden = true; }
            }

            bool isDashOrb = currentOrb && (currentOrb->getType() == GameObjectType::DashRing || currentOrb->getType() == GameObjectType::GravityDashRing || currentOrb->m_objectID == 1704 || currentOrb->m_objectID == 1751);

            if (p->m_isDashing) { wantHold = (diedIdle || sIdle < 15); }
            else if (ag->singleClickTimer > 0) { ag->singleClickTimer--; wantHold = true; }
            else if (currentOrb && currentOrb != ag->lastClickedOrb) {
                bool orbDeadly = false;
                if (!ag->checkpoints.empty()) { for (auto& op : ag->checkpoints.back().deadlyOrbs) if (op.getDistance(currentOrb->getPosition()) < 15.f) orbDeadly = true; }
                if (!orbDeadly) {
                    ag->lastClickedOrb = currentOrb; ag->singleClickTimer = isDashOrb ? 3 : 1; wantHold = true;
                    ag->orbChain.push_back({ currentOrb, curPos, p->m_yVelocity, p->m_isUpsideDown, ag->currentFrame, ag->currentFrame + 4 });
                }
            } else if (!forbidden && canActNow && (p->m_isOnGround || isHoldOnlyMode || isClickOnlyMode)) {
                bool safeOne = !isHoldOnlyMode && !diedOne && !ag->oneClickPath.empty();
                bool safeHold = holdAllowed && !isClickOnlyMode && !diedHold && !ag->holdPath.empty();
                if (diedIdle) {
                    if (safeOne) {
                        if (AutoBot::humanizeClicks && ag->humanDelayCounter < 2 && p->m_isOnGround) ag->humanDelayCounter++;
                        else { ag->humanDelayCounter = 0; ag->singleClickTimer = 1; wantHold = true; }
                    } else if (safeHold && sHold > sIdle + 8) wantHold = true;
                } else if (AutoBot::collectCoins && !coinIdle && ((coinOne && safeOne) || (coinHold && safeHold))) {
                    if (coinOne && safeOne) { ag->singleClickTimer = 1; wantHold = true; } else if (holdAllowed) wantHold = true;
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
                    cp.singleClickFails++;
                    if (cp.singleClickFails >= 3) cp.allowHoldJump = true;
                    for (auto it = ag->orbChain.rbegin(); it != ag->orbChain.rend(); ++it) {
                        if (isOrbAlwaysFatal(*it)) cp.deadlyOrbs.push_back(it->orb->getPosition());
                        else break;
                    }
                    for (auto it = ag->actions.rbegin(); it != ag->actions.rend(); ++it) {
                        if (it->hold) { for (int k = 0; k < AutoBot::learningSpeed; ++k) cp.failedJumpFrames.insert(it->frame + k); break; }
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