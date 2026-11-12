#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
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
    int forcedBranch;
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;

    int currentFrame = 0;
    bool currentHold = false;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
    CCDrawNode* trajectoryNode = nullptr;

    void setHold(GJBaseGameLayer* layer, bool hold) {
        if (currentHold != hold) {
            currentHold = hold;
            actions.push_back({ currentFrame, hold });
            if (hold) layer->pushButton(1, true);
            else layer->releaseButton(1, true);
        }
    }

    void saveMacro(const std::string& levelName) {
        auto path = Mod::get()->getSaveDir() / (levelName + ".txt");
        std::ofstream file(path.native());
        for (const auto& act : actions) {
            file << act.frame << " " << act.hold << "\n";
        }
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
    void onToggleRecord(CCObject* s) {
        AutoBot::isRecording = !static_cast<CCMenuItemToggler*>(s)->isToggled();
        if (AutoBot::isRecording) { AutoBot::isPlaying = false; AutoBot::actions.clear(); AutoBot::checkpoints.clear(); }
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

struct BranchResult {
    bool survived;
    int survivalSteps;
    std::vector<CCPoint> points;
};

enum class SimMode { Cube, Ship, Ball, Ufo, Wave };

SimMode getPlayerMode(PlayerObject* p) {
    if (p->m_isShip) return SimMode::Ship;
    if (p->m_isBall) return SimMode::Ball;
    if (p->m_isBird) return SimMode::Ufo;
    if (p->m_isDart) return SimMode::Wave;
    return SimMode::Cube;
}

class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<GameObject*> nearbyObjects;
    };

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        AutoBot::trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(AutoBot::trajectoryNode, 1000);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        AutoBot::currentHold = false;
        if (AutoBot::trajectoryNode) AutoBot::trajectoryNode->clear();

        if (AutoBot::isRecording && !AutoBot::checkpoints.empty()) {
            AutoBot::currentFrame = AutoBot::checkpoints.back().frame;
            AutoBot::actions.resize(AutoBot::checkpoints.back().actionCount);
        } else {
            AutoBot::currentFrame = 0;
            if (AutoBot::isRecording) { AutoBot::actions.clear(); AutoBot::checkpoints.clear(); }
        }
    }

    void collectNearbyObjects(float playerX) {
        m_fields->nearbyObjects.clear();
        if (!this->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(this->m_objects)) {
            if (!obj) continue;
            float ox = obj->getPositionX();
            if (ox >= playerX - 30.f && ox <= playerX + 600.f) m_fields->nearbyObjects.push_back(obj);
        }
    }
    BranchResult simulateBranch(PlayerObject* player, const std::vector<bool>& holdPattern) {
        BranchResult res{ true, 0, {} };
        CCPoint pos = player->getPosition();
        double yVel = player->m_yVelocity;
        bool upsideDown = player->m_isUpsideDown;
        float gravDir = upsideDown ? -1.0f : 1.0f;
        SimMode mode = getPlayerMode(player);

        float halfSize = 15.0f * player->getScale();
        float groundY = 90.0f + halfSize;
        float ceilingY = 390.0f - halfSize;
        float mult = (player->m_playerSpeed > 0.1f) ? player->m_playerSpeed : 1.0f;
        float speedX = 5.18f * mult;
        bool onSurface = player->m_isOnGround;

        for (size_t step = 0; step < holdPattern.size(); ++step) {
            CCPoint prevPos = pos;
            bool isHolding = holdPattern.at(step);
            bool prevHolding = (step > 0) ? holdPattern.at(step - 1) : false;
            bool justClicked = isHolding && !prevHolding;

            if (mode == SimMode::Ship) {
                yVel += (isHolding ? 0.38 : -0.34) * gravDir;
                yVel = std::clamp(yVel, -6.4, 6.4);
            } else if (mode == SimMode::Ufo) {
                if (justClicked) yVel = 7.8 * gravDir;
                yVel -= 0.52 * gravDir;
            } else if (mode == SimMode::Wave) {
                yVel = (isHolding ? 1.0 : -1.0) * speedX * 1.15f * gravDir;
            } else if (mode == SimMode::Ball) {
                if (justClicked && onSurface) {
                    upsideDown = !upsideDown; gravDir = upsideDown ? -1.0f : 1.0f; yVel = 3.2 * gravDir; onSurface = false;
                }
                yVel -= 0.78 * gravDir;
            } else {
                if (onSurface) {
                    yVel = 0.0;
                    if (isHolding) { yVel = 11.15 * gravDir; onSurface = false; }
                } else {
                    yVel -= 0.86 * gravDir;
                }
            }

            pos.x += speedX;
            pos.y += static_cast<float>(yVel) * 0.45f;
            onSurface = false;

            if (gravDir > 0 && pos.y <= groundY) { pos.y = groundY; if (mode != SimMode::Wave) { yVel = 0.0; onSurface = true; } }
            if (gravDir < 0 && pos.y >= ceilingY) { pos.y = ceilingY; if (mode != SimMode::Wave) { yVel = 0.0; onSurface = true; } }

            CCRect pBox = { pos.x - halfSize * 0.85f, pos.y - halfSize * 0.85f, halfSize * 1.7f, halfSize * 1.7f };
            for (GameObject* obj : m_fields->nearbyObjects) {
                CCRect oBox = obj->getObjectRect();
                if (!pBox.intersectsRect(oBox)) continue;

                int id = obj->m_objectID;
                auto type = obj->m_objectType;

                if (id == 35) { yVel = 15.8 * gravDir; onSurface = false; }
                else if (id == 140) { yVel = 11.2 * gravDir; onSurface = false; }
                else if (id == 1332) { yVel = 19.5 * gravDir; onSurface = false; }
                else if (id == 67) { upsideDown = !upsideDown; gravDir = upsideDown ? -1.0f : 1.0f; yVel = 4.5 * gravDir; onSurface = false; }
                else if (justClicked && (id == 36 || id == 141 || id == 1333)) {
                    yVel = (id == 1333 ? 15.5 : (id == 36 ? 11.2 : 8.5)) * gravDir; onSurface = false;
                } else if (id == 10 || type == GameObjectType::NormalGravityPortal) { upsideDown = false; gravDir = 1.0f; }
                else if (id == 11 || type == GameObjectType::InverseGravityPortal) { upsideDown = true; gravDir = -1.0f; }
                else if (id == 12 || type == GameObjectType::CubePortal) mode = SimMode::Cube;
                else if (id == 13 || type == GameObjectType::ShipPortal) mode = SimMode::Ship;
                else if (id == 47 || type == GameObjectType::BallPortal) mode = SimMode::Ball;
                else if (id == 111 || type == GameObjectType::UfoPortal) mode = SimMode::Ufo;
                else if (id == 660 || type == GameObjectType::WavePortal) mode = SimMode::Wave;
                else if (type == GameObjectType::Hazard) {
                    res.survived = false; res.points.push_back(pos); return res;
                } else if (type == GameObjectType::Solid && !obj->m_isDecoration) {
                    if (gravDir > 0 && prevPos.y >= oBox.getMaxY() + halfSize * 0.25f) {
                        pos.y = oBox.getMaxY() + halfSize; yVel = 0.0; onSurface = true;
                    } else if (gravDir < 0 && prevPos.y <= oBox.getMinY() - halfSize * 0.25f) {
                        pos.y = oBox.getMinY() - halfSize; yVel = 0.0; onSurface = true;
                    } else {
                        res.survived = false; res.points.push_back(pos); return res;
                    }
                }
            }
            res.survivalSteps++;
            res.points.push_back(pos);
        }
        return res;
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1 || this->m_player1->m_isDead) return;

        AutoBot::currentFrame++;
        auto player = this->m_player1;
        CCPoint startPos = player->getPosition();
        collectNearbyObjects(startPos.x);

        const int steps = 85;
        std::vector<std::vector<bool>> patterns(6, std::vector<bool>(steps, false));
        std::fill(patterns.at(1).begin(), patterns.at(1).end(), true);
        for (int i = 0; i < 3; ++i) patterns.at(2).at(i) = true;
        for (int i = 8; i < 11; ++i) patterns.at(3).at(i) = true;
        for (int i = 0; i < 3; ++i) patterns.at(4).at(i) = true;
        for (int i = 18; i < 21; ++i) patterns.at(4).at(i) = true;
        patterns.at(5) = patterns.at(4);
        for (int i = 36; i < 39; ++i) patterns.at(5).at(i) = true;

        std::vector<BranchResult> results(6);
        for (int i = 0; i < 6; ++i) results.at(i) = simulateBranch(player, patterns.at(i));

        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories) {
                std::vector<ccColor4F> colors = {
                    { 0.1f, 1.0f, 0.1f, 0.8f }, { 1.0f, 0.1f, 0.1f, 0.8f },
                    { 1.0f, 0.9f, 0.1f, 0.8f }, { 0.1f, 0.9f, 1.0f, 0.8f },
                    { 0.8f, 0.2f, 1.0f, 0.8f }, { 1.0f, 0.5f, 0.0f, 0.8f }
                };
                for (int i = 0; i < 6; ++i) {
                    CCPoint prev = startPos;
                    for (const auto& pt : results.at(i).points) {
                        AutoBot::trajectoryNode->drawSegment(prev, pt, 0.45f, colors.at(i));
                        prev = pt;
                    }
                }
            }
        }

        if (AutoBot::isRecording) {
            if (!this->m_isPracticeMode) this->togglePracticeMode(true);
            unsigned int cpCount = this->m_checkpoints ? this->m_checkpoints->count() : 0;
            while (AutoBot::checkpoints.size() < cpCount) {
                AutoBot::checkpoints.push_back({ AutoBot::currentFrame, AutoBot::actions.size(), -1 });
            }
            while (AutoBot::checkpoints.size() > cpCount && !AutoBot::checkpoints.empty()) {
                AutoBot::checkpoints.pop_back();
            }

            bool isFlight = player->m_isShip || player->m_isDart;
            int chosen = 0;
            if (!AutoBot::checkpoints.empty() && AutoBot::checkpoints.back().forcedBranch != -1) {
                chosen = AutoBot::checkpoints.back().forcedBranch;
                if (AutoBot::currentFrame - AutoBot::checkpoints.back().frame > 12) AutoBot::checkpoints.back().forcedBranch = -1;
            } else if (!isFlight && results.at(0).survivalSteps > 16) {
                chosen = 0;
            } else {
                std::vector<int> priority = { 0, 2, 3, 4, 5, 1 };
                int bestSteps = -1;
                for (int idx : priority) {
                    if (results.at(idx).survived) { chosen = idx; break; }
                    if (results.at(idx).survivalSteps > bestSteps) { bestSteps = results.at(idx).survivalSteps; chosen = idx; }
                }
            }
            AutoBot::setHold(this, patterns.at(chosen).at(0));
        }

        if (AutoBot::isPlaying) {
            for (const auto& act : AutoBot::actions) {
                if (act.frame == AutoBot::currentFrame) {
                    if (act.hold) this->pushButton(1, true);
                    else this->releaseButton(1, true);
                }
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (AutoBot::isRecording && !AutoBot::checkpoints.empty()) {
            auto& cp = AutoBot::checkpoints.back();
            cp.forcedBranch = (cp.forcedBranch == -1) ? 2 : (cp.forcedBranch + 1);
            if (cp.forcedBranch > 5) cp.forcedBranch = 0;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (AutoBot::isRecording) {
            AutoBot::isRecording = false;
            AutoBot::saveMacro(this->m_level->m_levelName);
        }
    }
};