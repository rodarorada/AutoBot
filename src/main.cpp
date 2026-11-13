#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
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
    bool active;
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
    bool botInput = false;
    bool isWaitingForKey = false;

    // Настраиваемые параметры игрока
    int scanStep = 5;           // Шаг фейковых кадров (настраивается в меню)
    int maxAgents = 4;          // Максимум квантовых агентов
    enumKeyCodes switchKey = KEY_Tab; // Клавиша переключения агентов

    int activeAgentIndex = 0;
    int nextAgentId = 1;
    std::vector<QuantumAgent> agents;

    CCDrawNode* trajectoryNode = nullptr;
    PlayerObject* nopIdle = nullptr;   // Невидимый отобразитель бездействия
    PlayerObject* nopHold = nullptr;   // Невидимый отобразитель зажатия

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

    bool init() {
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
    void keyDown(enumKeyCodes key) override {
        if (AutoBot::isWaitingForKey) {
            if (key != KEY_Escape) AutoBot::switchKey = key;
            AutoBot::isWaitingForKey = false;
            refreshLabels();
            return;
        }
        FLAlertLayer::keyDown(key);
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

class $modify(MyGameObject, GameObject) {
    void activateObject(PlayerObject* player) {
        if (AutoBot::isSimulating) {
            bool prev1 = this->m_hasBeenActivated;
            bool prev2 = this->m_hasBeenActivatedP2;
            GameObject::activateObject(player);
            this->m_hasBeenActivated = prev1;
            this->m_hasBeenActivatedP2 = prev2;
            return;
        }
        GameObject::activateObject(player);
    }
};

class $modify(MyPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (AutoBot::isSimulating || this == AutoBot::nopIdle || this == AutoBot::nopHold) {
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
    void copyPhysics(PlayerObject* dst, PlayerObject* src) {
        if (!dst || !src) return;
        dst->m_isDead = false;
        dst->m_position = src->m_position;
        dst->m_lastPosition = src->m_lastPosition;
        dst->setPosition(src->getPosition());
        dst->m_yVelocity = src->m_yVelocity;
        dst->m_xVelocity = src->m_xVelocity;
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
        if (!nop || !this->m_player1) return 0;

        copyPhysics(nop, this->m_player1);
        nop->m_position = startPos;
        nop->m_lastPosition = startPos;
        nop->setPosition(startPos);
        nop->m_yVelocity = startYVel;
        nop->m_isUpsideDown = startUpside;

        AutoBot::isSimulating = true;
        if (hold) nop->pushButton(PlayerButton::Jump);
        else nop->releaseButton(PlayerButton::Jump);

        int survived = 0;
        const float subDt = (1.0f / 60.0f) / 4.0f;

        for (int s = 0; s < steps; ++s) {
            for (int sub = 0; sub < 4; ++sub) {
                nop->m_lastPosition = nop->m_position;
                CCPoint before = nop->m_position;
                nop->update(subDt);
                if (std::abs(nop->m_position.x - before.x) < 0.001f) {
                    float mult = (nop->m_playerSpeed > 0.1f) ? nop->m_playerSpeed : 1.0f;
                    nop->m_position.x += 311.58f * mult * subDt;
                }
                if (std::abs(nop->m_position.y - before.y) < 0.0001f) {
                    nop->m_position.y += static_cast<float>(nop->m_yVelocity) * subDt;
                }
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

        nop->releaseButton(PlayerButton::Jump);
        AutoBot::isSimulating = false;
        return survived;
    }

    void trySpawnQuantumBranch(QuantumAgent& parent, CCPoint branchPos, double branchYVel) {
        if (static_cast<int>(AutoBot::agents.size()) >= AutoBot::maxAgents) return;
        for (const auto& ag : AutoBot::agents) {
            if (std::abs(ag.savedPos.x - branchPos.x) < 25.0f && std::abs(ag.savedPos.y - branchPos.y) < 20.0f) return;
        }

        std::vector<CCPoint> scoutIdle, scoutHold;
        float d1 = -1.f, d2 = -1.f;
        int surv = runNOP(AutoBot::nopIdle, branchPos, branchYVel, parent.savedUpsideDown, false, 65, scoutIdle, d1);
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
        AutoBot::trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(AutoBot::trajectoryNode, 1000);

        AutoBot::nopIdle = PlayerObject::create(1, 1, this, this->m_objectLayer, true);
        AutoBot::nopHold = PlayerObject::create(1, 1, this, this->m_objectLayer, true);
        if (AutoBot::nopIdle) { AutoBot::nopIdle->setVisible(false); this->m_objectLayer->addChild(AutoBot::nopIdle); }
        if (AutoBot::nopHold) { AutoBot::nopHold->setVisible(false); this->m_objectLayer->addChild(AutoBot::nopHold); }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (AutoBot::trajectoryNode) AutoBot::trajectoryNode->clear();
        if (!this->m_player1) return;

        if (AutoBot::isRecording) {
            if (AutoBot::agents.empty()) {
                QuantumAgent root{ AutoBot::nextAgentId++, true, 0, false, this->m_player1->getPosition(), 0.0, false, {}, {}, {}, {}, -1.f };
                AutoBot::agents.push_back(root);
                AutoBot::activeAgentIndex = 0;
            }
            if (auto* ag = AutoBot::getActiveAgent()) {
                if (!ag->checkpoints.empty()) {
                    auto& cp = ag->checkpoints.back();
                    ag->currentFrame = cp.frame;
                    ag->actions.resize(cp.actionCount);
                    if (!cp.inFineZoom) {
                        cp.inFineZoom = true;
                        cp.fineCheckFrame = cp.lastAliveFakeFrame;
                    } else {
                        cp.fineCheckFrame++;
                    }
                } else {
                    ag->currentFrame = 0;
                    ag->actions.clear();
                }
            }
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1 || this->m_player1->m_isDead || AutoBot::isSimulating) return;

        static bool prevSwitchPressed = false;
        bool switchPressed = CCKeyboardDispatcher::get()->getIsKeyPressed(AutoBot::switchKey);
        if (switchPressed && !prevSwitchPressed && !AutoBot::agents.empty()) {
            AutoBot::cycleAgent();
            if (auto* ag = AutoBot::getActiveAgent()) {
                this->m_player1->m_position = ag->savedPos;
                this->m_player1->setPosition(ag->savedPos);
                this->m_player1->m_yVelocity = ag->savedYVel;
                this->m_player1->m_isUpsideDown = ag->savedUpsideDown;
            }
        }
        prevSwitchPressed = switchPressed;

        auto* ag = AutoBot::getActiveAgent();
        if (!ag && AutoBot::isRecording) return;

        CCPoint curPos = this->m_player1->getPosition();
        double curYVel = this->m_player1->m_yVelocity;
        bool curUp = this->m_player1->m_isUpsideDown;

        if (ag) {
            ag->currentFrame++;
            ag->savedPos = curPos;
            ag->savedYVel = curYVel;
            ag->savedUpsideDown = curUp;

            bool shouldScan = (ag->currentFrame % AutoBot::scanStep == 0) || ag->idlePath.empty();
            if (!ag->checkpoints.empty() && ag->checkpoints.back().inFineZoom) shouldScan = true;

            if (shouldScan) {
                float dIdle = -1.f, dHold = -1.f;
                int sIdle = runNOP(AutoBot::nopIdle, curPos, curYVel, curUp, false, 90, ag->idlePath, dIdle);
                int sHold = runNOP(AutoBot::nopHold, curPos, curYVel, curUp, true, 90, ag->holdPath, dHold);
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

        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories && ag) {
                auto draw = [&](const std::vector<CCPoint>& pts, ccColor4F col) {
                    CCPoint p = curPos;
                    for (const auto& pt : pts) { AutoBot::trajectoryNode->drawSegment(p, pt, 0.5f, col); p = pt; }
                };
                draw(ag->idlePath, { 0.1f, 1.0f, 0.1f, 0.9f });
                draw(ag->holdPath, { 1.0f, 0.15f, 0.15f, 0.9f });
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (AutoBot::isSimulating || player == AutoBot::nopIdle || player == AutoBot::nopHold) {
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