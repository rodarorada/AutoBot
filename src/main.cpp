#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <vector>
#include <fstream>
#include <algorithm>

using namespace geode::prelude;

struct MacroAction {
    int frame;
    bool hold;
};

struct BotCheckpoint {
    int frame;
    size_t actionCount;
    int triedMask;
    int activeBranch;
};

struct BranchResult {
    bool survived;
    int survivalSteps;
    std::vector<CCPoint> points;
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;
    bool isSimulating = false;
    bool botInput = false;

    int currentFrame = 0;
    int graceFrames = 0;
    bool currentHold = false;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
    CCDrawNode* trajectoryNode = nullptr;

    void setHold(GJBaseGameLayer* layer, bool hold) {
        if (currentHold != hold) {
            currentHold = hold;
            actions.push_back({ currentFrame, hold });
            botInput = true;
            layer->handleButton(hold, 1, true);
            botInput = false;
        }
    }

    void saveMacro(const std::string& levelName) {
        auto path = Mod::get()->getSaveDir() / (levelName + ".txt");
        std::ofstream file(path.native());
        for (const auto& act : actions) file << act.frame << " " << act.hold << "\n";
    }
}
class AutoBotPopup : public FLAlertLayer {
protected:
    bool init() {
        if (!FLAlertLayer::init(150)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({ 290.f, 200.f });
        bg->setPosition(winSize / 2.f);
        m_mainLayer->addChild(bg);

        auto title = CCLabelBMFont::create("AutoBot Control Panel", "goldFont.fnt");
        title->setScale(0.7f);
        title->setPosition({ winSize.width / 2.f, winSize.height / 2.f + 75.f });
        m_mainLayer->addChild(title);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(menu);

        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.8f);
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(AutoBotPopup::onClose));
        closeBtn->setPosition({ winSize.width / 2.f - 135.f, winSize.height / 2.f + 90.f });
        menu->addChild(closeBtn);

        auto addToggle = [&](const char* labelText, float offsetY, bool initialState, SEL_MenuHandler callback) {
            auto label = CCLabelBMFont::create(labelText, "bigFont.fnt");
            label->setScale(0.45f);
            label->setAnchorPoint({ 0.f, 0.5f });
            label->setPosition({ winSize.width / 2.f - 120.f, winSize.height / 2.f + offsetY });
            m_mainLayer->addChild(label);

            auto onSpr = ButtonSprite::create("ON", "goldFont.fnt", "GJ_button_01.png", 0.55f);
            auto offSpr = ButtonSprite::create("OFF", "goldFont.fnt", "GJ_button_06.png", 0.55f);
            auto toggler = CCMenuItemToggler::create(offSpr, onSpr, this, callback);
            toggler->toggle(initialState);
            toggler->setPosition({ winSize.width / 2.f + 95.f, winSize.height / 2.f + offsetY });
            menu->addChild(toggler);
        };

        addToggle("Show path:", 25.f, AutoBot::showTrajectories, menu_selector(AutoBotPopup::onTogglePaths));
        addToggle("AutoBot Path Finder:", -15.f, AutoBot::isRecording, menu_selector(AutoBotPopup::onToggleRecord));
        addToggle("Playback (Showcase):", -55.f, AutoBot::isPlaying, menu_selector(AutoBotPopup::onTogglePlay));

        this->setKeypadEnabled(true);
        return true;
    }

    void onClose(CCObject*) { this->keyBackClicked(); }
    void onTogglePaths(CCObject* s) { AutoBot::showTrajectories = !static_cast<CCMenuItemToggler*>(s)->isToggled(); }

    void releaseBotInput() {
        AutoBot::graceFrames = 0;
        AutoBot::currentHold = false;
        if (auto pl = PlayLayer::get()) {
            AutoBot::botInput = true;
            pl->handleButton(false, 1, true);
            AutoBot::botInput = false;
        }
    }

    void onToggleRecord(CCObject* s) {
        AutoBot::isRecording = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        releaseBotInput();
        if (AutoBot::isRecording) {
            AutoBot::isPlaying = false;
            AutoBot::actions.clear();
            AutoBot::checkpoints.clear();
        }
    }

    void onTogglePlay(CCObject* s) {
        AutoBot::isPlaying = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        releaseBotInput();
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
class $modify(MyPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (AutoBot::isSimulating) {
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
        if (!AutoBot::isRecording && !AutoBot::isPlaying) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        if (AutoBot::isSimulating) return;
        if (AutoBot::botInput) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }
    }
};

class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        AutoBot::trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(AutoBot::trajectoryNode, 1000);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (AutoBot::trajectoryNode) AutoBot::trajectoryNode->clear();

        if (AutoBot::isRecording) {
            AutoBot::currentHold = false;
            AutoBot::graceFrames = 10;
            AutoBot::botInput = true;
            this->handleButton(false, 1, true);
            AutoBot::botInput = false;

            if (!AutoBot::checkpoints.empty()) {
                AutoBot::currentFrame = AutoBot::checkpoints.back().frame;
                AutoBot::actions.resize(AutoBot::checkpoints.back().actionCount);
            } else {
                AutoBot::currentFrame = 0;
                AutoBot::actions.clear();
            }
        } else {
            AutoBot::currentFrame = 0;
        }
    }

    BranchResult simulateBranch(PlayerObject* player, const std::vector<bool>& holdPattern) {
        BranchResult res{ true, 0, {} };

        CCPoint origPos = player->m_position;
        CCPoint origLastPos = player->m_lastPosition;
        double origYVel = player->m_yVelocity;
        double origGravity = player->m_gravity;
        bool origOnGround = player->m_isOnGround;
        bool origUpsideDown = player->m_isUpsideDown;
        bool origShip = player->m_isShip;
        bool origBall = player->m_isBall;
        bool origBird = player->m_isBird;
        bool origDart = player->m_isDart;
        bool origRobot = player->m_isRobot;
        bool origSpider = player->m_isSpider;
        bool origHolding = player->m_isHolding;
        float origRot = player->getRotation();

        bool streakEnabled = player->m_isTrailEnabled;
        if (player->m_regularTrail) player->m_regularTrail->setVisible(false);

        AutoBot::isSimulating = true;
        bool simHolding = AutoBot::isRecording ? AutoBot::currentHold : false;
        float mult = (player->m_playerSpeed > 0.1f) ? player->m_playerSpeed : 1.0f;
        const float subDt = (1.0f / 60.0f) / 4.0f;

        for (size_t step = 0; step < holdPattern.size(); ++step) {
            bool wantHold = holdPattern[step];
            if (wantHold && !simHolding) { player->pushButton(PlayerButton::Jump); simHolding = true; }
            else if (!wantHold && simHolding) { player->releaseButton(PlayerButton::Jump); simHolding = false; }

            for (int sub = 0; sub < 4; ++sub) {
                player->m_lastPosition = player->m_position;
                player->updateJump(subDt);
                player->update(subDt);
                player->m_position.x += 311.58f * mult * subDt;
                player->m_position.y += static_cast<float>(player->m_yVelocity) * subDt;
                this->checkCollisions(player, subDt, false);
                if (player->m_isDead) break;
            }

            res.points.push_back(player->m_position);
            if (player->m_isDead) { res.survived = false; break; }
            res.survivalSteps++;
        }
        if (simHolding != origHolding) {
            if (origHolding) player->pushButton(PlayerButton::Jump);
            else player->releaseButton(PlayerButton::Jump);
        }

        player->m_isDead = false;
        player->m_position = origPos;
        player->m_lastPosition = origLastPos;
        player->setPosition(origPos);
        player->m_yVelocity = origYVel;
        player->m_gravity = origGravity;
        player->m_isOnGround = origOnGround;
        player->m_isUpsideDown = origUpsideDown;
        player->m_isShip = origShip;
        player->m_isBall = origBall;
        player->m_isBird = origBird;
        player->m_isDart = origDart;
        player->m_isRobot = origRobot;
        player->m_isSpider = origSpider;
        player->m_isHolding = origHolding;
        player->setRotation(origRot);

        if (player->m_regularTrail && streakEnabled) {
            player->m_regularTrail->reset();
            player->m_regularTrail->setVisible(true);
        }

        AutoBot::isSimulating = false;
        return res;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1 || this->m_player1->m_isDead || AutoBot::isSimulating) return;

        AutoBot::currentFrame++;
        auto player = this->m_player1;
        CCPoint startPos = player->getPosition();

        const int steps = 85;
        std::vector<std::vector<bool>> patterns(6, std::vector<bool>(steps, false));
        std::fill(patterns[1].begin(), patterns[1].end(), true);
        for (int i = 0; i < 3; ++i) patterns[2][i] = true;
        for (int i = 6; i < 9; ++i) patterns[3][i] = true;
        for (int i = 14; i < 17; ++i) patterns[4][i] = true;
        for (int i = 0; i < 3; ++i) patterns[5][i] = true;
        for (int i = 20; i < 23; ++i) patterns[5][i] = true;

        bool isFlight = player->m_isShip || player->m_isDart;
        int branchCount = isFlight ? 2 : 6;

        std::vector<BranchResult> results(branchCount);
        for (int i = 0; i < branchCount; ++i) results[i] = simulateBranch(player, patterns[i]);

        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories) {
                std::vector<ccColor4F> colors = {
                    { 0.1f, 1.0f, 0.1f, 0.85f }, { 1.0f, 0.1f, 0.1f, 0.85f },
                    { 1.0f, 0.9f, 0.1f, 0.85f }, { 0.1f, 0.9f, 1.0f, 0.85f },
                    { 0.8f, 0.2f, 1.0f, 0.85f }, { 1.0f, 0.5f, 0.0f, 0.85f }
                };
                float halfBox = 15.0f * player->getScale();
                for (int i = 0; i < branchCount; ++i) {
                    const auto& pts = results[i].points;
                    if (pts.empty()) continue;
                    CCPoint prev = startPos;
                    for (const auto& pt : pts) {
                        AutoBot::trajectoryNode->drawSegment(prev, pt, 0.45f, colors[i]);
                        prev = pt;
                    }
                    CCPoint endPt = pts.back();
                    ccColor4F boxCol = results[i].survived ? colors[i] : ccColor4F{ 1.f, 0.f, 0.f, 0.95f };
                    AutoBot::trajectoryNode->drawRect(
                        { endPt.x - halfBox, endPt.y - halfBox },
                        { endPt.x + halfBox, endPt.y + halfBox },
                        { 0, 0, 0, 0 }, 0.5f, boxCol
                    );
                }
            }
        }

        if (AutoBot::isRecording) {
            if (!this->m_isPracticeMode) this->togglePracticeMode(true);
            unsigned int cpCount = this->m_checkpointArray ? this->m_checkpointArray->count() : 0;
            while (AutoBot::checkpoints.size() < cpCount) AutoBot::checkpoints.push_back({ AutoBot::currentFrame, AutoBot::actions.size(), 0, -1 });
            while (AutoBot::checkpoints.size() > cpCount && !AutoBot::checkpoints.empty()) AutoBot::checkpoints.pop_back();

            if (AutoBot::graceFrames > 0) {
                AutoBot::graceFrames--;
                AutoBot::setHold(this, false);
            } else {
                int mask = AutoBot::checkpoints.empty() ? 0 : AutoBot::checkpoints.back().triedMask;
                int chosen = 0, bestScore = -999;
                for (int idx = 0; idx < branchCount; ++idx) {
                    int penalty = (mask & (1 << idx)) ? 40 : 0;
                    if (!isFlight && idx != 0 && results[0].survivalSteps > 16 && !(mask & 1)) penalty += 50;
                    int score = results[idx].survivalSteps - penalty;
                    if (score > bestScore) { bestScore = score; chosen = idx; }
                }
                if (!AutoBot::checkpoints.empty()) AutoBot::checkpoints.back().activeBranch = chosen;
                AutoBot::setHold(this, patterns[chosen][0]);
            }
        }

        if (AutoBot::isPlaying) {
            for (const auto& act : AutoBot::actions) {
                if (act.frame == AutoBot::currentFrame) {
                    AutoBot::botInput = true;
                    this->handleButton(act.hold, 1, true);
                    AutoBot::botInput = false;
                }
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (AutoBot::isSimulating) { player->m_isDead = true; return; }
        if (AutoBot::isRecording && !AutoBot::checkpoints.empty()) {
            auto& cp = AutoBot::checkpoints.back();
            if (cp.activeBranch != -1) cp.triedMask |= (1 << cp.activeBranch);
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (AutoBot::isRecording) { AutoBot::isRecording = false; AutoBot::saveMacro(this->m_level->m_levelName); }
    }
};