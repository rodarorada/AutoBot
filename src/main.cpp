#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <vector>
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
};

struct QuantumAgent {
    int id;
    int currentFrame;
    bool currentHold;
    CCPoint savedPos;
    double savedYVel;
    bool savedUpsideDown;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
    std::vector<CCPoint> idlePath;
    std::vector<CCPoint> holdPath;
    float deathX;
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;
    bool isSimulating = false;
    bool isScouting = false;
    bool botInput = false;
    bool isWaitingForKey = false;
    bool switchRequested = false;

    int scanStep = 5;
    int maxAgents = 4;
    int scoutFrames = 0;
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
    CCLabelBMFont* m_keyLabel = nullptr;

    bool init() override {
        if (!FLAlertLayer::init(150)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({ 330.f, 240.f });
        bg->setPosition(winSize / 2.f);
        m_mainLayer->addChild(bg);

        auto title = CCLabelBMFont::create("Quantum AutoBot Settings", "goldFont.fnt");
        title->setScale(0.65f);
        title->setPosition({ winSize.width / 2.f, winSize.height / 2.f + 100.f });
        m_mainLayer->addChild(title);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(menu);

        auto closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"), this, menu_selector(AutoBotPopup::onClose)
        );
        closeBtn->setPosition({ winSize.width / 2.f - 150.f, winSize.height / 2.f + 108.f });
        menu->addChild(closeBtn);

        auto addToggle = [&](const char* text, float y, bool state, SEL_MenuHandler cb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.4f);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 140.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(lbl);

            auto tog = CCMenuItemToggler::create(
                ButtonSprite::create("OFF", "goldFont.fnt", "GJ_button_06.png", 0.5f),
                ButtonSprite::create("ON", "goldFont.fnt", "GJ_button_01.png", 0.5f), this, cb
            );
            tog->toggle(state);
            tog->setPosition({ winSize.width / 2.f + 115.f, winSize.height / 2.f + y });
            menu->addChild(tog);
        };

        addToggle("Show NOP Paths:", 65.f, AutoBot::showTrajectories, menu_selector(AutoBotPopup::onTogglePaths));
        addToggle("Quantum Path Finder:", 35.f, AutoBot::isRecording, menu_selector(AutoBotPopup::onToggleRecord));
        addToggle("Playback (Showcase):", 5.f, AutoBot::isPlaying, menu_selector(AutoBotPopup::onTogglePlay));

        auto addStepper = [&](const char* text, float y, CCLabelBMFont*& valLbl, SEL_MenuHandler decCb, SEL_MenuHandler incCb) {
            auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
            lbl->setScale(0.4f);
            lbl->setAnchorPoint({ 0.f, 0.5f });
            lbl->setPosition({ winSize.width / 2.f - 140.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(lbl);

            auto minus = CCMenuItemSpriteExtra::create(ButtonSprite::create("-", "goldFont.fnt", "GJ_button_04.png", 0.45f), this, decCb);
            minus->setPosition({ winSize.width / 2.f + 75.f, winSize.height / 2.f + y });
            menu->addChild(minus);

            valLbl = CCLabelBMFont::create("0", "goldFont.fnt");
            valLbl->setScale(0.55f);
            valLbl->setPosition({ winSize.width / 2.f + 105.f, winSize.height / 2.f + y });
            m_mainLayer->addChild(valLbl);

            auto plus = CCMenuItemSpriteExtra::create(ButtonSprite::create("+", "goldFont.fnt", "GJ_button_04.png", 0.45f), this, incCb);
            plus->setPosition({ winSize.width / 2.f + 135.f, winSize.height / 2.f + y });
            menu->addChild(plus);
        };

        addStepper("Fake Frame Step:", -28.f, m_stepLabel, menu_selector(AutoBotPopup::onDecStep), menu_selector(AutoBotPopup::onIncStep));
        addStepper("Max Quantum Agents:", -58.f, m_agentsLabel, menu_selector(AutoBotPopup::onDecAgents), menu_selector(AutoBotPopup::onIncAgents));

        auto bindLbl = CCLabelBMFont::create("Switch Agent Key:", "bigFont.fnt");
        bindLbl->setScale(0.4f);
        bindLbl->setAnchorPoint({ 0.f, 0.5f });
        bindLbl->setPosition({ winSize.width / 2.f - 140.f, winSize.height / 2.f - 90.f });
        m_mainLayer->addChild(bindLbl);

        m_keyLabel = CCLabelBMFont::create("TAB", "goldFont.fnt");
        m_keyLabel->setScale(0.5f);
        auto keyBtn = CCMenuItemSpriteExtra::create(m_keyLabel, this, menu_selector(AutoBotPopup::onBindKey));
        keyBtn->setPosition({ winSize.width / 2.f + 105.f, winSize.height / 2.f - 90.f });
        menu->addChild(keyBtn);

        refreshLabels();
        this->setKeypadEnabled(true);
        return true;
    }

    void refreshLabels() {
        if (m_stepLabel) m_stepLabel->setString(std::to_string(AutoBot::scanStep).c_str());
        if (m_agentsLabel) m_agentsLabel->setString(std::to_string(AutoBot::maxAgents).c_str());
        if (m_keyLabel) {
            if (AutoBot::isWaitingForKey) m_keyLabel->setString("[PRESS KEY]");
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
    void onBindKey(CCObject*) { AutoBot::isWaitingForKey = true; refreshLabels(); }

    void onToggleRecord(CCObject* s) {
        AutoBot::isRecording = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        AutoBot::isScouting = false;
        AutoBot::scoutFrames = 0;
        FMODAudioEngine::sharedEngine()->pauseAllMusic(false);
        if (auto pl = PlayLayer::get()) {
            AutoBot::botInput = true;
            pl->handleButton(false, 1, true);
            AutoBot::botInput = false;
        }
        if (AutoBot::isRecording) {
            AutoBot::isPlaying = false;
            AutoBot::agents.clear();
            AutoBot::nextAgentId = 1;
            AutoBot::isScouting = true;
            AutoBot::scoutFrames = 75;
        }
    }

    void onTogglePlay(CCObject* s) {
        AutoBot::isPlaying = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        AutoBot::isScouting = false;
        FMODAudioEngine::sharedEngine()->pauseAllMusic(false);
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
        PlayerObject* nopIdle = nullptr;
        PlayerObject* nopHold = nullptr;
    };

    void copyPhysics(PlayerObject* dst, PlayerObject* src) {
        if (!dst || !src) return;
        dst->m_isDead = false;
        dst->m_position = src->m_position;
        dst->m_lastPosition = src->m_lastPosition;
        dst->setPosition(src->getPosition());
        dst->m_yVelocity = src->m_yVelocity;
        dst->m_gravity = src->m_gravity;
        dst->m_playerSpeed = src->m_playerSpeed;
        dst->m_isOnGround = src->m_isOnGround;
        dst->m_isUpsideDown = src->m_isUpsideDown;
        dst->m_isShip = src->m_isShip;
        dst->m_isBall = src->m_isBall;
        dst->m_isBird = src->m_isBird;
        dst->m_isDart = src->m_isDart;
        dst->m_isRobot = src->m_isRobot;
        dst->m_isSpider = src->m_isSpider;
        dst->m_isDashing = src->m_isDashing;
        dst->setScale(src->getScale());
        dst->setRotation(src->getRotation());
    }

    int runNOP(PlayerObject* nop, CCPoint startPos, double startYVel, bool startUpside, bool hold, int steps, std::vector<CCPoint>& pts, float& outDeathX) {
        pts.clear();
        outDeathX = -1.0f;
        PlayerObject* realPlayer = this->m_player1;
        if (!nop || !realPlayer) return 0;

        copyPhysics(nop, realPlayer);
        nop->m_position = startPos;
        nop->m_lastPosition = startPos;
        nop->setPosition(startPos);
        nop->m_yVelocity = startYVel;
        nop->m_isUpsideDown = startUpside;

        AutoBot::isSimulating = true;
        this->m_player1 = nop;

        int survived = 0;
        const float subDt = (1.0f / 60.0f) / 4.0f;
        float mult = (nop->m_playerSpeed > 0.1f) ? nop->m_playerSpeed : 1.0f;

        for (int s = 0; s < steps; ++s) {
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
                    if (hold && s == 0 && sub == 0) nop->m_yVelocity = 430.0 * gravDir;
                    nop->m_yVelocity -= 1950.0 * gravDir * subDt;
                } else {
                    if (hold && nop->m_isOnGround) {
                        nop->m_yVelocity = 602.0 * gravDir;
                        nop->m_isOnGround = false;
                    }
                    nop->m_yVelocity -= 2865.0 * gravDir * subDt;
                    if (gravDir > 0 && nop->m_yVelocity < -810.0) nop->m_yVelocity = -810.0;
                    if (gravDir < 0 && nop->m_yVelocity > 810.0) nop->m_yVelocity = 810.0;
                }

                nop->m_position.x += 311.58f * mult * subDt;
                nop->m_position.y += static_cast<float>(nop->m_yVelocity) * subDt;
                nop->setPosition(nop->m_position);
                this->checkCollisions(nop, subDt, true);
                if (nop->m_isDead) break;
            }
            pts.push_back(nop->getPosition());
            if (nop->m_isDead) {
                outDeathX = nop->getPosition().x;
                break;
            }
            survived++;
        }

        this->m_player1 = realPlayer;
        AutoBot::isSimulating = false;
        return survived;
    }
    void trySpawnQuantumBranch(QuantumAgent& parent, CCPoint branchPos, double branchYVel) {
        if (static_cast<int>(AutoBot::agents.size()) >= AutoBot::maxAgents) return;
        for (const auto& ag : AutoBot::agents) {
            if (std::abs(ag.savedPos.x - branchPos.x) < 25.0f && std::abs(ag.savedPos.y - branchPos.y) < 20.0f) return;
        }
        std::vector<CCPoint> scoutIdle;
        float d1 = -1.f;
        int surv = runNOP(m_fields->nopIdle, branchPos, branchYVel, parent.savedUpsideDown, false, 65, scoutIdle, d1);
        if (surv >= 45 && !scoutIdle.empty()) {
            float endY = scoutIdle.back().y;
            float baseEndY = parent.idlePath.empty() ? branchPos.y : parent.idlePath.back().y;
            if (std::abs(endY - baseEndY) > 28.0f) {
                QuantumAgent child = parent;
                child.id = AutoBot::nextAgentId++;
                child.savedPos = branchPos;
                child.savedYVel = branchYVel;
                child.actions.push_back({ parent.currentFrame, true });
                child.currentHold = true;
                AutoBot::agents.push_back(child);
            }
        }
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        m_fields->trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(m_fields->trajectoryNode, 1000);

        m_fields->nopIdle = PlayerObject::create(1, 1, this, this->m_objectLayer, true);
        m_fields->nopHold = PlayerObject::create(1, 1, this, this->m_objectLayer, true);
        if (m_fields->nopIdle) { m_fields->nopIdle->setVisible(false); this->m_objectLayer->addChild(m_fields->nopIdle); }
        if (m_fields->nopHold) { m_fields->nopHold->setVisible(false); this->m_objectLayer->addChild(m_fields->nopHold); }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_fields->trajectoryNode) m_fields->trajectoryNode->clear();
        if (!this->m_player1) return;

        if (AutoBot::agents.empty()) {
            AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, -1.f });
            AutoBot::activeAgentIndex = 0;
        }
        if (auto* ag = AutoBot::getActiveAgent()) {
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

    void update(float dt) {
        if (AutoBot::isRecording && AutoBot::isScouting && this->m_player1) {
            FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
            CCPoint holdPos = this->m_player1->m_position;
            if (auto* ag = AutoBot::getActiveAgent()) {
                float d1 = -1.f, d2 = -1.f;
                int stepLen = std::min(90, 91 - AutoBot::scoutFrames);
                runNOP(m_fields->nopIdle, holdPos, 0.0, this->m_player1->m_isUpsideDown, false, stepLen, ag->idlePath, d1);
                runNOP(m_fields->nopHold, holdPos, 0.0, this->m_player1->m_isUpsideDown, true, stepLen, ag->holdPath, d2);
            }
            this->m_player1->m_position = holdPos;
            this->m_player1->setPosition(holdPos);
            this->m_player1->m_yVelocity = 0.0;
            AutoBot::scoutFrames--;
            if (AutoBot::scoutFrames <= 0) {
                AutoBot::isScouting = false;
                FMODAudioEngine::sharedEngine()->pauseAllMusic(false);
            }
            return;
        }
        PlayLayer::update(dt);
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

        if (AutoBot::agents.empty()) {
            AutoBot::agents.push_back({ AutoBot::nextAgentId++, 0, false, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, -1.f });
        }

        auto* ag = AutoBot::getActiveAgent();
        CCPoint curPos = this->m_player1->getPosition();
        double curYVel = this->m_player1->m_yVelocity;
        bool curUp = this->m_player1->m_isUpsideDown;

        if (ag && !AutoBot::isScouting) {
            ag->currentFrame++;
            ag->savedPos = curPos;
            ag->savedYVel = curYVel;
            ag->savedUpsideDown = curUp;

            bool shouldScan = (ag->currentFrame % AutoBot::scanStep == 0) || ag->idlePath.empty();
            if (!ag->checkpoints.empty() && ag->checkpoints.back().inFineZoom) shouldScan = true;

            if (shouldScan) {
                float dIdle = -1.f, dHold = -1.f;
                int sIdle = runNOP(m_fields->nopIdle, curPos, curYVel, curUp, false, 90, ag->idlePath, dIdle);
                int sHold = runNOP(m_fields->nopHold, curPos, curYVel, curUp, true, 90, ag->holdPath, dHold);
                ag->deathX = dIdle;

                if (sIdle >= 85 && !ag->checkpoints.empty()) {
                    ag->checkpoints.back().lastAliveFakeFrame = ag->currentFrame;
                    ag->checkpoints.back().inFineZoom = false;
                } else if (dIdle > 0.f && !ag->checkpoints.empty()) {
                    ag->checkpoints.back().deathFakeFrame = ag->currentFrame;
                }

                if (sHold > 35 && !ag->holdPath.empty()) {
                    trySpawnQuantumBranch(*ag, ag->holdPath[std::min<size_t>(12, ag->holdPath.size() - 1)], curYVel);
                }

                if (AutoBot::isRecording) {
                    if (!this->m_isPracticeMode) this->togglePracticeMode(true);
                    bool wantHold = (sHold > sIdle + 8);
                    if (!ag->checkpoints.empty() && ag->checkpoints.back().inFineZoom) {
                        wantHold = (ag->currentFrame >= ag->checkpoints.back().fineCheckFrame);
                    }
                    if (ag->currentHold != wantHold) {
                        ag->currentHold = wantHold;
                        ag->actions.push_back({ ag->currentFrame, wantHold });
                        AutoBot::botInput = true;
                        this->handleButton(wantHold, 1, true);
                        AutoBot::botInput = false;
                    }
                }
            }
        }

        if (m_fields->trajectoryNode) {
            m_fields->trajectoryNode->clear();
            if (AutoBot::showTrajectories && ag) {
                auto draw = [&](const std::vector<CCPoint>& pts, ccColor4F col) {
                    CCPoint p = curPos;
                    for (const auto& pt : pts) { m_fields->trajectoryNode->drawSegment(p, pt, 0.5f, col); p = pt; }
                };
                draw(ag->idlePath, { 0.1f, 1.0f, 0.1f, 0.9f });
                draw(ag->holdPath, { 1.0f, 0.15f, 0.15f, 0.9f });
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (AutoBot::isSimulating) {
            if (player) player->m_isDead = true;
            return;
        }
        if (AutoBot::isRecording) {
            if (auto* ag = AutoBot::getActiveAgent()) {
                if (!ag->checkpoints.empty()) {
                    auto& cp = ag->checkpoints.back();
                    if (cp.inFineZoom && cp.fineCheckFrame > cp.deathFakeFrame && AutoBot::agents.size() > 1) {
                        AutoBot::agents.erase(AutoBot::agents.begin() + AutoBot::activeAgentIndex);
                        AutoBot::activeAgentIndex = 0;
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