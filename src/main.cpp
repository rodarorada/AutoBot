#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

struct MacroAction {
    int frame;
    bool hold;
};

struct BotState {
    int frame;
    CCPoint pos;
    double yVelocity;
    bool isUpsideDown;
    size_t actionCount;
    int triedBranch;
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;

    int currentFrame = 0;
    bool currentHold = false;
    std::vector<MacroAction> actions;
    std::vector<BotState> checkpoints;
    CCDrawNode* trajectoryNode = nullptr;

    void setHold(PlayerObject* player, bool hold) {
        if (currentHold != hold) {
            currentHold = hold;
            actions.push_back({ currentFrame, hold });
            if (hold) player->pushButton(PlayerButton::Jump);
            else player->releaseButton(PlayerButton::Jump);
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
// Вспомогательная структура для быстрого результата симуляции одной ветки
struct BranchResult {
    bool survived;          // Выжила ли ветка до конца прогноза
    int survivalSteps;      // Сколько шагов прожила до смерти
    float finalX;           // Насколько далеко продвинулась по X
    std::vector<CCPoint> points;
};

// Определяем текущий режим игрока в виде числа для симуляции
enum class SimMode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider };

SimMode getPlayerMode(PlayerObject* p) {
    if (p->m_isShip) return SimMode::Ship;
    if (p->m_isBall) return SimMode::Ball;
    if (p->m_isBird) return SimMode::Ufo;
    if (p->m_isDart) return SimMode::Wave;
    if (p->m_isRobot) return SimMode::Robot;
    if (p->m_isSpider) return SimMode::Spider;
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
        AutoBot::currentFrame = 0;
        AutoBot::currentHold = false;
        if (AutoBot::trajectoryNode) AutoBot::trajectoryNode->clear();
    }

    // Собираем только объекты в зоне видимости пути (оптимизация FPS)
    void collectNearbyObjects(float playerX) {
        m_fields->nearbyObjects.clear();
        if (!this->m_objects) return;
        for (auto obj : CCArrayExt<GameObject*>(this->m_objects)) {
            if (!obj || obj->m_isDecoration) continue;
            float ox = obj->getPositionX();
            if (ox >= playerX - 40.f && ox <= playerX + 380.f) {
                m_fields->nearbyObjects.push_back(obj);
            }
        }
    }

    BranchResult simulateBranch(PlayerObject* player, const std::vector<bool>& holdPattern) {
        BranchResult res{ true, 0, player->getPositionX(), {} };
        CCPoint pos = player->getPosition();
        double yVel = player->m_yVelocity;
        bool upsideDown = player->m_isUpsideDown;
        float gravDir = upsideDown ? -1.0f : 1.0f;
        SimMode mode = getPlayerMode(player);

        float scale = player->getScale();
        float halfSize = 15.0f * scale;
        float groundY = (this->m_groundLayer ? this->m_groundLayer->getPositionY() : 0.0f) + halfSize;
        float ceilingY = 285.0f;
        float speedX = 5.18f * player->m_playerSpeed;

        for (size_t step = 0; step < holdPattern.size(); ++step) {
            CCPoint prevPos = pos;
            bool isHolding = holdPattern[step];
            bool prevHolding = (step > 0) ? holdPattern[step - 1] : false;
            bool justClicked = isHolding && !prevHolding;

            bool onGround = (!upsideDown && pos.y <= groundY + 0.5f) || (upsideDown && pos.y >= ceilingY - 0.5f);

            // Физика полёта/прыжка в зависимости от режима
            if (mode == SimMode::Ship) {
                yVel += (isHolding ? 0.52 : -0.48) * gravDir;
                yVel = std::clamp(yVel, -8.5, 8.5);
            } else if (mode == SimMode::Ufo) {
                if (justClicked) yVel = 8.1 * gravDir;
                yVel -= 0.55 * gravDir;
            } else if (mode == SimMode::Wave) {
                yVel = (isHolding ? 1.0 : -1.0) * speedX * 1.15f * gravDir;
            } else if (mode == SimMode::Ball) {
                if (justClicked && onGround) {
                    upsideDown = !upsideDown;
                    gravDir = upsideDown ? -1.0f : 1.0f;
                    yVel = 3.2 * gravDir;
                }
                yVel -= 0.78 * gravDir;
            } else {
                if (onGround) {
                    yVel = 0.0;
                    if (isHolding) yVel = 11.15 * gravDir;
                } else {
                    yVel -= 0.86 * gravDir;
                }
            }

            pos.x += speedX;
            pos.y += static_cast<float>(yVel) * 0.45f;

            if (gravDir > 0 && pos.y < groundY) { pos.y = groundY; if (mode != SimMode::Wave) yVel = 0.0; }
            if (gravDir < 0 && pos.y > ceilingY) { pos.y = ceilingY; if (mode != SimMode::Wave) yVel = 0.0; }

            // Проверка столкновений с блоками, шипами, падами, орбами и порталами
            CCRect pBox = { pos.x - halfSize * 0.85f, pos.y - halfSize * 0.85f, halfSize * 1.7f, halfSize * 1.7f };
            for (GameObject* obj : m_fields->nearbyObjects) {
                CCRect oBox = obj->getObjectRect();
                if (!pBox.intersectsRect(oBox)) continue;

                auto type = obj->m_objectType;
                if (type == GameObjectType::Hazard) {
                    res.survived = false;
                    res.points.push_back(pos);
                    return res;
                } else if (type == GameObjectType::Solid) {
                    float topY = oBox.getMaxY() + halfSize;
                    float botY = oBox.getMinY() - halfSize;
                    // Если приземляемся сверху (при обычной гравитации) или снизу (при перевёрнутой)
                    if (gravDir > 0 && prevPos.y >= oBox.getMaxY() + halfSize * 0.4f) {
                        pos.y = topY;
                        yVel = 0.0;
                    } else if (gravDir < 0 && prevPos.y <= oBox.getMinY() - halfSize * 0.4f) {
                        pos.y = botY;
                        yVel = 0.0;
                    } else {
                        // Врезались в стенку блока
                        res.survived = false;
                        res.points.push_back(pos);
                        return res;
                    }
                } else if (type == GameObjectType::YellowJumpPad) {
                    yVel = 15.5 * gravDir;
                } else if (type == GameObjectType::PinkJumpPad) {
                    yVel = 11.2 * gravDir;
                } else if (type == GameObjectType::GravityPad) {
                    upsideDown = !upsideDown; gravDir = upsideDown ? -1.0f : 1.0f; yVel = 5.0 * gravDir;
                } else if (justClicked && (type == GameObjectType::YellowJumpRing || type == GameObjectType::PinkJumpRing)) {
                    yVel = (type == GameObjectType::YellowJumpRing ? 11.2 : 8.5) * gravDir;
                } else if (type == GameObjectType::InverseGravityPortal) {
                    upsideDown = true; gravDir = -1.0f;
                } else if (type == GameObjectType::NormalGravityPortal) {
                    upsideDown = false; gravDir = 1.0f;
                } else if (type == GameObjectType::CubePortal) mode = SimMode::Cube;
                else if (type == GameObjectType::ShipPortal) mode = SimMode::Ship;
                else if (type == GameObjectType::BallPortal) mode = SimMode::Ball;
                else if (type == GameObjectType::UfoPortal) mode = SimMode::Ufo;
                else if (type == GameObjectType::WavePortal) mode = SimMode::Wave;
            }

            res.survivalSteps++;
            res.finalX = pos.x;
            res.points.push_back(pos);
        }
        return res;
    }
    void drawBranchLine(CCPoint startPos, const BranchResult& res, ccColor4F color) {
        if (!AutoBot::trajectoryNode || res.points.empty()) return;
        CCPoint prev = startPos;
        for (const auto& pt : res.points) {
            AutoBot::trajectoryNode->drawSegment(prev, pt, 1.0f, color);
            prev = pt;
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1) return;

        AutoBot::currentFrame++;
        auto player = this->m_player1;
        CCPoint startPos = player->getPosition();

        // Один раз за кадр собираем объекты впереди
        collectNearbyObjects(startPos.x);

        const int steps = 45;
        std::vector<std::vector<bool>> patterns(6, std::vector<bool>(steps, false));
        // 0: Ничего не делать
        // 1: Зажать (Hold)
        std::fill(patterns[1].begin(), patterns[1].end(), true);
        // 2: 1 клик
        for (int i = 0; i < 3; ++i) patterns[2][i] = true;
        // 3: 2 клика
        patterns[3] = patterns[2];
        for (int i = 11; i < 14; ++i) patterns[3][i] = true;
        // 4: 3 клика
        patterns[4] = patterns[3];
        for (int i = 22; i < 25; ++i) patterns[4][i] = true;
        // 5: 4 клика
        patterns[5] = patterns[4];
        for (int i = 33; i < 36; ++i) patterns[5][i] = true;

        std::vector<BranchResult> results(6);
        for (int i = 0; i < 6; ++i) {
            results[i] = simulateBranch(player, patterns[i]);
        }

        // Отрисовка всех 6 веток по кнопке Show path
        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories) {
                ccColor4F colors[6] = {
                    { 0.1f, 1.0f, 0.1f, 0.75f }, { 1.0f, 0.1f, 0.1f, 0.75f },
                    { 1.0f, 0.9f, 0.1f, 0.75f }, { 0.1f, 0.9f, 1.0f, 0.75f },
                    { 0.8f, 0.2f, 1.0f, 0.75f }, { 1.0f, 0.5f, 0.0f, 0.75f }
                };
                for (int i = 0; i < 6; ++i) drawBranchLine(startPos, results[i], colors[i]);
            }
        }

        // Умный Path Finder (выбор безопасной ветки + сохранение чекпоинтов)
        if (AutoBot::isRecording) {
            int skipBranch = AutoBot::checkpoints.empty() ? -1 : AutoBot::checkpoints.back().triedBranch;
            int chosen = -1;

            // Ищем первую выжившую ветку (учитывая запрещённые после отката)
            for (int i = 0; i < 6; ++i) {
                if (i <= skipBranch) continue;
                if (results[i].survived) { chosen = i; break; }
            }
            // Если все выживают не до конца, берём ту, что пролетела дальше всех
            if (chosen == -1) {
                int bestSteps = -1;
                for (int i = 0; i < 6; ++i) {
                    if (i <= skipBranch) continue;
                    if (results[i].survivalSteps > bestSteps) { bestSteps = results[i].survivalSteps; chosen = i; }
                }
            }

            if (chosen != -1) {
                AutoBot::setHold(player, patterns[chosen][0]);
                if (AutoBot::currentFrame % 25 == 0 && results[0].survived) {
                    AutoBot::checkpoints.push_back({ AutoBot::currentFrame, startPos, player->m_yVelocity, player->m_isUpsideDown, AutoBot::actions.size(), -1 });
                }
            }
        }

        // Воспроизведение готового макроса (Showcase)
        if (AutoBot::isPlaying) {
            for (const auto& act : AutoBot::actions) {
                if (act.frame == AutoBot::currentFrame) {
                    if (act.hold) player->pushButton(PlayerButton::Jump);
                    else player->releaseButton(PlayerButton::Jump);
                }
            }
        }
    }

    // Перехват смерти для отката назад (Backtracking)
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (AutoBot::isRecording && !AutoBot::checkpoints.empty()) {
            auto& cp = AutoBot::checkpoints.back();
            cp.triedBranch++;
            if (cp.triedBranch >= 5 && AutoBot::checkpoints.size() > 1) {
                AutoBot::checkpoints.pop_back(); // Откатываемся ещё глубже назад
                AutoBot::checkpoints.back().triedBranch++;
            }
            auto& activeCp = AutoBot::checkpoints.back();
            AutoBot::currentFrame = activeCp.frame;
            AutoBot::actions.resize(activeCp.actionCount);
            player->setPosition(activeCp.pos);
            player->m_yVelocity = activeCp.yVelocity;
            player->m_isUpsideDown = activeCp.isUpsideDown;
            return; // Отменяем смерть!
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