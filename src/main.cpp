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

struct MacroAction {
    int frame;
    bool hold;
};

struct BotCheckpoint {
    int frame;
    size_t actionCount;
    int lastAliveFakeFrame;
    int deathFakeFrame;
    bool inFineZoom;
    int fineCheckFrame;
    std::set<int> failedJumpFrames;
};

struct QuantumAgent {
    int id;
    int currentFrame;
    bool currentHold;
    int singleClickTimer;
    CCPoint savedPos;
    double savedYVel;
    bool savedUpsideDown;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
    std::vector<CCPoint> lockedJumpArc;
    std::vector<CCPoint> idlePath;
    std::vector<CCPoint> holdPath;
    std::vector<CCPoint> oneClickPath;
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;
    bool collectCoins = true;
    bool isSimulating = false;
    bool botInput = false;
    bool isWaitingForKey = false;
    bool switchRequested = false;

    int scanStep = 4;
    int maxAgents = 4;
    int maxPath = 90;
    enumKeyCodes switchKey = KEY_Tab;

    int activeAgentIndex = 0;
    int nextAgentId = 1;
    std::vector<QuantumAgent> agents;

    QuantumAgent* getActiveAgent() {
        if (agents.empty()) return nullptr;
        if (activeAgentIndex < 0 || activeAgentIndex >= static_cast<int>(agents.size())) {
            activeAgentIndex = 0;
        }
        return &agents[activeAgentIndex];
    }

    void cycleAgent() {
        if (agents.size() <= 1) return;
        activeAgentIndex = (activeAgentIndex + 1) % static_cast<int>(agents.size());
    }

    void saveMacro(const std::string& levelName, const std::vector<MacroAction>& acts) {
        auto path = Mod::get()->getSaveDir() / (levelName + ".txt");
        std::ofstream file(path.native());
        for (const auto& act : acts) file << act.frame << " " << act.hold << "\n";
    }
}
class AutoBotPopup : public FLAlertLayer {
protected:
    CCLabelBMFont* m_stepLabel = nullptr;
    CCLabelBMFont* m_agentsLabel = nullptr;
    CCLabelBMFont* m_pathLabel = nullptr;
    CCLabelBMFont* m_keyLabel = nullptr;

    bool init() override {
        if (!FLAlertLayer::init(150)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({ 340.f, 285.f });
        bg->setPosition(winSize / 2.f);
        m_mainLayer->addChild(bg);

        auto title = CCLabelBMFont::create("Quantum AutoBot Settings", "goldFont.fnt");
        title->setScale(0.65f);
        title->setPosition({ winSize.width / 2.f, winSize.height / 2.f + 122.f });
        m_mainLayer->addChild(title);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(menu);

        auto closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"), this, menu_selector(AutoBotPopup::onClose)
        );
        closeBtn->setPosition({ winSize.width / 2.f - 155.f, winSize.height / 2.f + 130.f });
        menu->addChild(closeBtn);

        auto addToggle = [&](const char* text, float y, bool state, SEL_MenuHandler cb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.36f);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 145.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(lbl);

            auto tog = CCMenuItemToggler::create(
                ButtonSprite::create("OFF", "goldFont.fnt", "GJ_button_06.png", 0.45f),
                ButtonSprite::create("ON", "goldFont.fnt", "GJ_button_01.png", 0.45f), this, cb
            );
            tog->toggle(state);
            tog->setPosition({ winSize.width / 2.f + 120.f, winSize.height / 2.f + y });
            menu->addChild(tog);
        };

        addToggle("Show NOP Paths:", 92.f, AutoBot::showTrajectories, menu_selector(AutoBotPopup::onTogglePaths));
        addToggle("Quantum Path Finder:", 66.f, AutoBot::isRecording, menu_selector(AutoBotPopup::onToggleRecord));
        addToggle("Playback (Showcase):", 40.f, AutoBot::isPlaying, menu_selector(AutoBotPopup::onTogglePlay));
        addToggle("Collect Coins:", 14.f, AutoBot::collectCoins, menu_selector(AutoBotPopup::onToggleCoins));

        auto addStepper = [&](const char* text, float y, CCLabelBMFont*& valLbl, SEL_MenuHandler decCb, SEL_MenuHandler incCb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.36f);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 145.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(lbl);

            auto minus = CCMenuItemSpriteExtra::create(ButtonSprite::create("-", "goldFont.fnt", "GJ_button_04.png", 0.4f), this, decCb);
            minus->setPosition({ winSize.width / 2.f + 75.f, winSize.height / 2.f + y });
            menu->addChild(minus);

            valLbl = CCLabelBMFont::create("0", "goldFont.fnt");
            valLbl->setScale(0.48f);
            valLbl->setPosition({ winSize.width / 2.f + 108.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(valLbl);

            auto plus = CCMenuItemSpriteExtra::create(ButtonSprite::create("+", "goldFont.fnt", "GJ_button_04.png", 0.4f), this, incCb);
            plus->setPosition({ winSize.width / 2.f + 140.f, winSize.height / 2.f + y });
            menu->addChild(plus);
        };

        addStepper("Fake Frame Step:", -16.f, m_stepLabel, menu_selector(AutoBotPopup::onDecStep), menu_selector(AutoBotPopup::onIncStep));
        addStepper("Max Quantum Agents:", -44.f, m_agentsLabel, menu_selector(AutoBotPopup::onDecAgents), menu_selector(AutoBotPopup::onIncAgents));
        addStepper("Max Path Length:", -72.f, m_pathLabel, menu_selector(AutoBotPopup::onDecPath), menu_selector(AutoBotPopup::onIncPath));

        auto bindLbl = CCLabelBMFont::create("Switch Agent Key:", "bigFont.fnt");
        bindLbl->setScale(0.36f);
        bindLbl->setAnchorPoint({ 0.f, 0.5f });
        bindLbl->setPosition({ winSize.width / 2.f - 145.f, winSize.height / 2.f - 104.f });
        m_mainLayer->addChild(bindLbl);

        m_keyLabel = CCLabelBMFont::create("TAB", "goldFont.fnt");
        m_keyLabel->setScale(0.46f);
        auto keyBtn = CCMenuItemSpriteExtra::create(m_keyLabel, this, menu_selector(AutoBotPopup::onBindKey));
        keyBtn->setPosition({ winSize.width / 2.f + 108.f, winSize.height / 2.f - 104.f });
        menu->addChild(keyBtn);

        refreshLabels();
        this->setKeypadEnabled(true);
        return true;
    }

    void onToggleCoins(CCObject* s) { AutoBot::collectCoins = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }

    void refreshLabels() {
        if (m_stepLabel) m_stepLabel->setString(std::to_string(AutoBot::scanStep).c_str());
        if (m_agentsLabel) m_agentsLabel->setString(std::to_string(AutoBot::maxAgents).c_str());
        if (m_pathLabel) m_pathLabel->setString(std::to_string(AutoBot::maxPath).c_str());
        if (m_keyLabel) {
            if (AutoBot::isWaitingForKey) m_keyLabel->setString("[KEY]");
            else {
                const char* kn = CCKeyboardDispatcher::get()->keyToString(AutoBot::switchKey);
                m_keyLabel->setString(kn ? kn : "KEY");
            }
        }
    }
    void keyDown(enumKeyCodes key, double timestamp) override {
        if (AutoBot::isWaitingForKey) {
            if (key != KEY_Escape) AutoBot::switchKey = key;
            AutoBot::isWaitingForKey = false;
            refreshLabels();
            return;
        }
        FLAlertLayer::keyDown(key, timestamp);
    }

    void onClose(CCObject*) { AutoBot::isWaitingForKey = false; this->keyBackClicked(); }
    void onTogglePaths(CCObject* s) { AutoBot::showTrajectories = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }
    void onDecStep(CCObject*) { AutoBot::scanStep = std::max(1, AutoBot::scanStep - 1); refreshLabels(); }
    void onIncStep(CCObject*) { AutoBot::scanStep = std::min(20, AutoBot::scanStep + 1); refreshLabels(); }
    void onDecAgents(CCObject*) { AutoBot::maxAgents = std::max(1, AutoBot::maxAgents - 1); refreshLabels(); }
    void onIncAgents(CCObject*) { AutoBot::maxAgents = std::min(16, AutoBot::maxAgents + 1); refreshLabels(); }
    void onDecPath(CCObject*) { AutoBot::maxPath = std::max(30, AutoBot::maxPath - 15); refreshLabels(); }
    void onIncPath(CCObject*) { AutoBot::maxPath = std::min(240, AutoBot::maxPath + 15); refreshLabels(); }
    void onBindKey(CCObject*) { AutoBot::isWaitingForKey = true; refreshLabels(); }

    void onToggleRecord(CCObject* s) {
        AutoBot::isRecording = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        if (auto pl = PlayLayer::get()) {
            AutoBot::botInput = true;
            pl->handleButton(false, 1, true);
            AutoBot::botInput = false;
        }
        if (AutoBot::isRecording) {
            AutoBot::isPlaying = false;
            AutoBot::agents.clear();
            AutoBot::nextAgentId = 1;
        }
    }

    void onTogglePlay(CCObject* s) {
        AutoBot::isPlaying = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        if (AutoBot::isPlaying) AutoBot::isRecording = false;
    }

public:
    static AutoBotPopup* create() {
        auto ret = new AutoBotPopup();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(MyKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool arr, double timestamp) {
        if (down && !arr && !AutoBot::isWaitingForKey && key == AutoBot::switchKey) {
            AutoBot::switchRequested = true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, arr, timestamp);
    }
};

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({ winSize.width - 45.f, 45.f });
        this->addChild(menu);
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
        if (!AutoBot::isRecording && !AutoBot::isPlaying) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        if (AutoBot::botInput) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }
};

class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        CCDrawNode* trajectoryNode = nullptr;
        PlayerObject* nopSim = nullptr;
    };

    int runNOP(CCPoint startPos, double startYVel, bool startUpside, bool startGrounded, int mode, int steps, std::vector<CCPoint>& pts, bool& outDied, bool& outGotCoin) {
        pts.clear();
        outDied = false;
        outGotCoin = false;
        PlayerObject* realPlayer = this->m_player1;
        PlayerObject* nop = m_fields->nopSim;
        if (!nop || !realPlayer) return 0;

        nop->m_isDead = false;
        nop->m_position = startPos;
        nop->m_lastPosition = startPos;
        nop->setPosition(startPos);
        nop->m_yVelocity = startYVel;
        nop->m_gravity = realPlayer->m_gravity;
        nop->m_playerSpeed = realPlayer->m_playerSpeed;
        nop->m_isOnGround = startGrounded;
        nop->m_isUpsideDown = startUpside;
        nop->m_isShip = realPlayer->m_isShip;
        nop->m_isBall = realPlayer->m_isBall;
        nop->m_isBird = realPlayer->m_isBird;
        nop->m_isDart = realPlayer->m_isDart;
        nop->m_isRobot = realPlayer->m_isRobot;
        nop->m_isSpider = realPlayer->m_isSpider;
        nop->setScale(realPlayer->getScale());

        AutoBot::isSimulating = true;
        this->m_player1 = nop;

        int survived = 0;
        bool jumpedOnce = false;
        const float subDt = (1.0f / 60.0f) / 4.0f;
        float mult = (nop->m_playerSpeed > 0.1f) ? nop->m_playerSpeed : 1.0f;

        for (int s = 0; s < steps; ++s) {
            bool hold = (mode == 1) || (mode == 2 && !jumpedOnce);
            for (int sub = 0; sub < 4; ++sub) {
                nop->m_lastPosition = nop->m_position;
                float gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
                if (nop->m_yVelocity * gravDir > 15.0) nop->m_isOnGround = false;

                if (nop->m_isDart) {
                    nop->m_yVelocity = (hold ? 311.58f * mult : -311.58f * mult) * gravDir;
                } else if (nop->m_isShip) {
                    nop->m_yVelocity += (hold ? 1380.0f : -1120.0f) * gravDir * subDt;
                    nop->m_yVelocity = std::clamp(nop->m_yVelocity, -550.0, 550.0);
                } else if (nop->m_isBird) {
                    if (hold && s == 0 && sub == 0) { nop->m_yVelocity = 430.0 * gravDir; jumpedOnce = true; }
                    nop->m_yVelocity -= 1950.0 * gravDir * subDt;
                } else {
                    if (hold && nop->m_isOnGround) {
                        if (nop->m_isBall || nop->m_isSpider) {
                            nop->m_isUpsideDown = !nop->m_isUpsideDown;
                            gravDir = nop->m_isUpsideDown ? -1.0f : 1.0f;
                            nop->m_yVelocity = (nop->m_isSpider ? -1200.0 : -200.0) * gravDir;
                        } else {
                            nop->m_yVelocity = (nop->m_isRobot ? 640.0 : 602.0) * gravDir;
                        }
                        nop->m_isOnGround = false;
                        jumpedOnce = true;
                    }
                    nop->m_yVelocity -= 2865.0 * gravDir * subDt;
                    if (gravDir > 0 && nop->m_yVelocity < -810.0) nop->m_yVelocity = -810.0;
                    if (gravDir < 0 && nop->m_yVelocity > 810.0) nop->m_yVelocity = 810.0;
                }

                nop->m_position.x += 311.58f * mult * subDt;
                nop->m_position.y += static_cast<float>(nop->m_yVelocity) * subDt;
                nop->setPosition(nop->m_position);
                this->checkCollisions(nop, subDt, true);

                if (!nop->m_isDead && this->m_objects) {
                    CCRect pRect = { nop->m_position.x - 10.f, nop->m_position.y - 10.f, 20.f, 20.f };
                    for (auto* obj : CCArrayExt<GameObject*>(this->m_objects)) {
                        if (!obj || std::abs(obj->getPositionX() - nop->m_position.x) > 45.f) continue;
                        auto t = obj->getType();
                        if (t == GameObjectType::Hazard) {
                            if (pRect.intersectsRect(obj->getObjectRect())) { nop->m_isDead = true; break; }
                        } else if (t == GameObjectType::SecretCoin || t == GameObjectType::UserCoin) {
                            if (pRect.intersectsRect(obj->getObjectRect())) outGotCoin = true;
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

        this->m_player1 = realPlayer;
        AutoBot::isSimulating = false;
        return survived;
    }
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        m_fields->trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(m_fields->trajectoryNode, 1000);
        m_fields->nopSim = PlayerObject::create(1, 1, this, this->m_objectLayer, true);
        if (m_fields->nopSim) { m_fields->nopSim->setVisible(false); this->m_objectLayer->addChild(m_fields->nopSim); }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->trajectoryNode) m_fields->trajectoryNode->clear();
        if (!this->m_player1) return;

        if (AutoBot::agents.empty()) {
            AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, 0, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, {}, {} });
            AutoBot::activeAgentIndex = 0;
        }
        if (auto* ag = AutoBot::getActiveAgent()) {
            ag->lockedJumpArc.clear();
            ag->singleClickTimer = 0;
            if (AutoBot::isRecording && !ag->checkpoints.empty()) {
                auto& cp = ag->checkpoints.back();
                ag->currentFrame = cp.frame;
                ag->actions.resize(cp.actionCount);
                if (!cp.inFineZoom) { cp.inFineZoom = true; cp.fineCheckFrame = cp.lastAliveFakeFrame; }
                else cp.fineCheckFrame++;
            } else {
                ag->currentFrame = 0;
                ag->actions.clear();
            }
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1 || this->m_player1->m_isDead || AutoBot::isSimulating) return;

        if (AutoBot::switchRequested) {
            AutoBot::switchRequested = false;
            AutoBot::cycleAgent();
            if (auto* ag = AutoBot::getActiveAgent()) {
                this->m_player1->m_position = ag->savedPos;
                this->m_player1->setPosition(ag->savedPos);
                this->m_player1->m_yVelocity = ag->savedYVel;
                this->m_player1->m_isUpsideDown = ag->savedUpsideDown;
            }
        }

        if (m_fields->trajectoryNode) m_fields->trajectoryNode->clear();
        if (!AutoBot::showTrajectories && !AutoBot::isRecording) return;

        if (AutoBot::agents.empty()) {
            AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, 0, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, {}, {} });
        }

        auto* ag = AutoBot::getActiveAgent();
        auto p = this->m_player1;
        CCPoint curPos = p->getPosition();
        bool isGroundedMode = !p->m_isShip && !p->m_isDart && !p->m_isBird;
        bool diedIdle = false, diedHold = false, diedOne = false;
        bool coinIdle = false, coinHold = false, coinOne = false;

        if (isGroundedMode && !p->m_isOnGround) {
            if (ag->lockedJumpArc.empty()) {
                runNOP(curPos, p->m_yVelocity, p->m_isUpsideDown, false, 0, AutoBot::maxPath, ag->lockedJumpArc, diedIdle, coinIdle);
            }
            while (!ag->lockedJumpArc.empty() && ag->lockedJumpArc.front().x <= curPos.x + 1.0f) {
                ag->lockedJumpArc.erase(ag->lockedJumpArc.begin());
            }
            ag->idlePath = ag->lockedJumpArc;
        } else {
            ag->lockedJumpArc.clear();
            runNOP(curPos, p->m_yVelocity, p->m_isUpsideDown, p->m_isOnGround, 0, AutoBot::maxPath, ag->idlePath, diedIdle, coinIdle);
        }

        CCPoint jumpOrigin = curPos;
        bool canJumpNow = p->m_isOnGround || !isGroundedMode;
        if (!canJumpNow && !ag->idlePath.empty()) {
            for (size_t i = 1; i < ag->idlePath.size(); ++i) {
                if (std::abs(ag->idlePath[i].y - ag->idlePath[i - 1].y) < 0.25f) {
                    jumpOrigin = ag->idlePath[i]; canJumpNow = true; break;
                }
            }
        }

        ag->holdPath.clear();
        ag->oneClickPath.clear();
        if (canJumpNow) {
            runNOP(jumpOrigin, 0.0, p->m_isUpsideDown, true, 1, AutoBot::maxPath, ag->holdPath, diedHold, coinHold);
            runNOP(jumpOrigin, 0.0, p->m_isUpsideDown, true, 2, AutoBot::maxPath, ag->oneClickPath, diedOne, coinOne);
        }

        if (AutoBot::isRecording) {
            if (!this->m_isPracticeMode) this->togglePracticeMode(true);
            unsigned int cpCount = this->m_checkpointArray ? this->m_checkpointArray->count() : 0;
            while (ag->checkpoints.size() < cpCount) ag->checkpoints.push_back({ ag->currentFrame, ag->actions.size(), ag->currentFrame, 0, false, 0, {} });
            while (ag->checkpoints.size() > cpCount && !ag->checkpoints.empty()) ag->checkpoints.pop_back();

            ag->currentFrame++;
            ag->savedPos = curPos;
            ag->savedYVel = p->m_yVelocity;
            ag->savedUpsideDown = p->m_isUpsideDown;

            bool wantHold = false;
            bool frameForbidden = (!ag->checkpoints.empty() && ag->checkpoints.back().failedJumpFrames.count(ag->currentFrame));

            if (ag->singleClickTimer > 0) {
                ag->singleClickTimer--;
                wantHold = true;
            } else if (!frameForbidden && p->m_isOnGround) {
                if (AutoBot::collectCoins && !coinIdle && (coinOne || coinHold)) {
                    if (coinOne && !diedOne) { ag->singleClickTimer = 3; wantHold = true; }
                    else if (coinHold && !diedHold) { wantHold = true; }
                } else if (diedIdle && !ag->idlePath.empty()) {
                    float idleDeathX = ag->idlePath.back().x;
                    float oneEndX = ag->oneClickPath.empty() ? 0.f : ag->oneClickPath.back().x;
                    float holdEndX = ag->holdPath.empty() ? 0.f : ag->holdPath.back().x;
                    if (!diedOne && oneEndX > idleDeathX + 20.f) { ag->singleClickTimer = 3; wantHold = true; }
                    else if (!diedHold && holdEndX > idleDeathX + 25.f) { wantHold = true; }
                }
            }

            if (ag->currentHold != wantHold) {
                ag->currentHold = wantHold;
                ag->actions.push_back({ ag->currentFrame, wantHold });
                AutoBot::botInput = true;
                this->handleButton(wantHold, 1, true);
                AutoBot::botInput = false;
            }
        }

        if (AutoBot::showTrajectories && m_fields->trajectoryNode) {
            auto draw = [&](CCPoint start, const std::vector<CCPoint>& pts, ccColor4F col, bool died) {
                if (pts.empty()) return;
                CCPoint prev = start;
                for (const auto& pt : pts) { m_fields->trajectoryNode->drawSegment(prev, pt, 0.55f, col); prev = pt; }
                float hb = 15.0f * p->getScale();
                CCPoint end = pts.back();
                ccColor4F box = died ? ccColor4F{ 1.f, 0.f, 0.f, 0.95f } : col;
                m_fields->trajectoryNode->drawRect({ end.x - hb, end.y - hb }, { end.x + hb, end.y + hb }, { 0, 0, 0, 0 }, 0.5f, box);
            };
            draw(curPos, ag->idlePath, { 0.1f, 1.0f, 0.1f, 0.9f }, diedIdle);
            draw(jumpOrigin, ag->holdPath, { 1.0f, 0.12f, 0.12f, 0.9f }, diedHold);
            draw(jumpOrigin, ag->oneClickPath, { 1.0f, 0.65f, 0.0f, 0.98f }, diedOne);
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (AutoBot::isSimulating) { if (player) player->m_isDead = true; return; }
        if (AutoBot::isRecording) {
            if (auto* ag = AutoBot::getActiveAgent()) {
                if (!ag->checkpoints.empty() && !ag->actions.empty()) {
                    for (auto it = ag->actions.rbegin(); it != ag->actions.rend(); ++it) {
                        if (it->hold) { ag->checkpoints.back().failedJumpFrames.insert(it->frame); break; }
                    }
                }
            }
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (AutoBot::isRecording) {
            AutoBot::isRecording = false;
            if (auto* ag = AutoBot::getActiveAgent()) AutoBot::saveMacro(this->m_level->m_levelName, ag->actions);
        }
    }
};