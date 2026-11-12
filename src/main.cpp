#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <vector>
#include <fstream>
#include <algorithm>

using namespace geode::prelude;

struct MacroAction {
    int frame;
    bool hold;
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;

    int currentFrame = 0;
    std::vector<MacroAction> actions;
    CCDrawNode* trajectoryNode = nullptr;

    void saveMacro(const std::string& levelName) {
        auto path = Mod::get()->getSaveDir() / (levelName + ".txt");
        std::ofstream file(path.native());
        for (const auto& act : actions) {
            file << act.frame << " " << act.hold << "\n";
        }
    }
}

// Компактное окно настроек с переключателями ON / OFF (под Geode v6)
class AutoBotPopup : public geode::Popup {
protected:
    bool init() {
        if (!Popup::init(280.f, 190.f)) return false;

        this->setTitle("AutoBot Control Panel");

        auto winSize = m_mainLayer->getContentSize();
        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(menu);

        auto addToggle = [&](const char* labelText, float yPos, bool initialState, SEL_MenuHandler callback) {
            auto label = CCLabelBMFont::create(labelText, "bigFont.fnt");
            label->setScale(0.45f);
            label->setAnchorPoint({ 0.f, 0.5f });
            label->setPosition({ 30.f, yPos });
            m_mainLayer->addChild(label);

            auto onSpr = ButtonSprite::create("ON", "goldFont.fnt", "GJ_button_01.png", 0.6f);
            auto offSpr = ButtonSprite::create("OFF", "goldFont.fnt", "GJ_button_06.png", 0.6f);
            auto toggler = CCMenuItemToggler::create(offSpr, onSpr, this, callback);
            toggler->toggle(initialState);
            toggler->setPosition({ winSize.width - 55.f, yPos });
            menu->addChild(toggler);
        };

        addToggle("Show 6-Way Paths:", winSize.height - 65.f, AutoBot::showTrajectories, menu_selector(AutoBotPopup::onTogglePaths));
        addToggle("AutoBot Path Finder:", winSize.height - 105.f, AutoBot::isRecording, menu_selector(AutoBotPopup::onToggleRecord));
        addToggle("Playback (Showcase):", winSize.height - 145.f, AutoBot::isPlaying, menu_selector(AutoBotPopup::onTogglePlay));

        return true;
    }

    void onTogglePaths(CCObject* sender) {
        AutoBot::showTrajectories = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    }

    void onToggleRecord(CCObject* sender) {
        AutoBot::isRecording = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
        if (AutoBot::isRecording) {
            AutoBot::isPlaying = false;
            AutoBot::actions.clear();
        }
    }

    void onTogglePlay(CCObject* sender) {
        AutoBot::isPlaying = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
        if (AutoBot::isPlaying) {
            AutoBot::isRecording = false;
        }
    }

public:
    static AutoBotPopup* create() {
        auto ret = new AutoBotPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// Одна компактная кнопка в правом нижнем углу меню паузы
class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({ winSize.width - 45.f, 45.f });
        this->addChild(menu);

        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_optionsBtn_001.png", 1.f, CircleBaseColor::Green, CircleBaseSize::Small
        );
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(MyPauseLayer::onOpenAutoBot)
        );
        menu->addChild(btn);
    }

    void onOpenAutoBot(CCObject*) {
        AutoBotPopup::create()->show();
    }
};

// Игровой слой и симуляция 6 веток траекторий с учётом пола и гравитации
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;

        AutoBot::trajectoryNode = CCDrawNode::create();
        this->m_objectLayer->addChild(AutoBot::trajectoryNode, 1000);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        AutoBot::currentFrame = 0;
        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
        }
    }

    void simulateBranch(PlayerObject* player, const std::vector<bool>& holdPattern, ccColor4F color) {
        CCPoint pos = player->getPosition();
        double yVel = player->m_yVelocity;
        bool upsideDown = player->m_isUpsideDown;
        float gravDir = upsideDown ? -1.0f : 1.0f;
        float playerHalfHeight = 15.0f * player->getScale();
        float groundY = this->m_groundLayer ? this->m_groundLayer->getPositionY() + playerHalfHeight : playerHalfHeight;
        float ceilingY = 300.0f;
        float speedX = 5.18f * player->m_playerSpeed;

        for (size_t step = 0; step < holdPattern.size(); ++step) {
            CCPoint prevPos = pos;
            bool isHolding = holdPattern[step];
            bool prevHolding = (step > 0) ? holdPattern[step - 1] : false;
            bool justClicked = isHolding && !prevHolding;

            bool onGround = (!upsideDown && pos.y <= groundY + 0.5f) || (upsideDown && pos.y >= ceilingY - 0.5f);

            if (player->m_isShip || player->m_isBird) {
                if (player->m_isBird) {
                    if (justClicked) yVel = 8.0 * gravDir;
                    yVel -= 0.55 * gravDir;
                } else {
                    yVel += (isHolding ? 0.52 : -0.48) * gravDir;
                    yVel = std::clamp(yVel, -8.5, 8.5);
                }
            } else if (player->m_isDart) {
                yVel = (isHolding ? 1.0 : -1.0) * speedX * 1.15f * gravDir;
            } else if (player->m_isBall) {
                if (justClicked && onGround) {
                    gravDir = -gravDir;
                    yVel = 3.0 * gravDir;
                }
                yVel -= 0.75 * gravDir;
            } else {
                if (onGround) {
                    yVel = 0.0;
                    if (isHolding) {
                        yVel = 11.15 * gravDir;
                    }
                } else {
                    yVel -= 0.86 * gravDir;
                }
            }

            pos.x += speedX;
            pos.y += static_cast<float>(yVel) * 0.45f;

            if (gravDir > 0 && pos.y < groundY) {
                pos.y = groundY;
                if (!player->m_isDart) yVel = 0.0;
            }

            AutoBot::trajectoryNode->drawSegment(prevPos, pos, 1.0f, color);
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1) return;

        AutoBot::currentFrame++;
        auto player = this->m_player1;

        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories) {
                const int steps = 45;
                std::vector<bool> pNone(steps, false);
                std::vector<bool> pHold(steps, true);
                std::vector<bool> pClick1(steps, false);
                for (int i = 0; i < 3; ++i) pClick1[i] = true;
                std::vector<bool> pClick2 = pClick1;
                for (int i = 10; i < 13; ++i) pClick2[i] = true;
                std::vector<bool> pClick3 = pClick2;
                for (int i = 20; i < 23; ++i) pClick3[i] = true;
                std::vector<bool> pClick4 = pClick3;
                for (int i = 30; i < 33; ++i) pClick4[i] = true;

                simulateBranch(player, pNone,   { 0.1f, 1.0f, 0.1f, 0.75f });
                simulateBranch(player, pHold,   { 1.0f, 0.1f, 0.1f, 0.75f });
                simulateBranch(player, pClick1, { 1.0f, 0.9f, 0.1f, 0.75f });
                simulateBranch(player, pClick2, { 0.1f, 0.9f, 1.0f, 0.75f });
                simulateBranch(player, pClick3, { 0.8f, 0.2f, 1.0f, 0.75f });
                simulateBranch(player, pClick4, { 1.0f, 0.5f, 0.0f, 0.75f });
            }
        }

        if (AutoBot::isPlaying) {
            for (const auto& act : AutoBot::actions) {
                if (act.frame == AutoBot::currentFrame) {
                    if (act.hold) player->pushButton(PlayerButton::Jump);
                    else player->releaseButton(PlayerButton::Jump);
                }
            }
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (AutoBot::isRecording) {
            AutoBot::isRecording = false;
            AutoBot::saveMacro(this->m_level->m_levelName);
        }
    }
};