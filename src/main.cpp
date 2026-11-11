#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <vector>
#include <fstream>

using namespace geode::prelude;

// Структура одного действия (кадр и нажатие)
struct MacroAction {
    int frame;
    bool hold;
};

// Глобальное состояние нашего бота
namespace AutoBot {
    bool isRecording = false;       // Режим записи / поиска пути
    bool isPlaying = false;         // Режим воспроизведения (шоукейс)
    bool showTrajectories = true;   // Отрисовка линий путей

    int currentFrame = 0;
    std::vector<MacroAction> actions;
    CCDrawNode* trajectoryNode = nullptr;

    // Функция сохранения макроса в файл для шоукейса
    void saveMacro(const std::string& levelName) {
        auto path = Mod::get()->getSaveDir() / (levelName + ".txt");
        std::ofstream file(path.native());
        for (const auto& act : actions) {
            file << act.frame << " " << act.hold << "\n";
        }
        log::info("Макрос сохранён в: {}", path);
    }
}

// 1. Добавляем кнопки управления в меню паузы
class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = CCMenu::create();
        menu->setPosition({ 50.f, 160.f });
        this->addChild(menu);

        // Кнопка включения/выключения отрисовки путей
        auto sprPaths = ButtonSprite::create("Paths", "goldFont.fnt", "GJ_button_01.png", 0.7f);
        auto btnPaths = CCMenuItemSpriteExtra::create(
            sprPaths, this, menu_selector(MyPauseLayer::onTogglePaths)
        );
        btnPaths->setPosition({ 0.f, 40.f });
        menu->addChild(btnPaths);

        // Кнопка переключения режима (Запись / Воспроизведение)
        auto sprMode = ButtonSprite::create("Bot Mode", "goldFont.fnt", "GJ_button_04.png", 0.7f);
        auto btnMode = CCMenuItemSpriteExtra::create(
            sprMode, this, menu_selector(MyPauseLayer::onToggleMode)
        );
        btnMode->setPosition({ 0.f, -10.f });
        menu->addChild(btnMode);
    }

    void onTogglePaths(CCObject*) {
        AutoBot::showTrajectories = !AutoBot::showTrajectories;
        FLAlertLayer::create(
            "AutoBot",
            AutoBot::showTrajectories ? "Отрисовка путей: ВКЛ" : "Отрисовка путей: ВЫКЛ",
            "OK"
        )->show();
    }

    void onToggleMode(CCObject*) {
        if (!AutoBot::isRecording && !AutoBot::isPlaying) {
            AutoBot::isRecording = true;
            AutoBot::actions.clear();
            FLAlertLayer::create("AutoBot", "Режим: ПОИСК ПУТИ / ЗАПИСЬ", "OK")->show();
        } else if (AutoBot::isRecording) {
            AutoBot::isRecording = false;
            AutoBot::isPlaying = true;
            FLAlertLayer::create("AutoBot", "Режим: ВОСПРОИЗВЕДЕНИЕ (Шоукейс)", "OK")->show();
        } else {
            AutoBot::isPlaying = false;
            FLAlertLayer::create("AutoBot", "Бот ВЫКЛЮЧЕН", "OK")->show();
        }
    }
};

// 2. Перехватываем игровой процесс для расчёта путей и нажатий
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;

        // Создаём холст для рисования линий поверх уровня
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

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1) return;

        AutoBot::currentFrame++;
        auto player = this->m_player1;
        CCPoint pos = player->getPosition();

        // Отрисовка двух базовых прогнозов траектории по кнопке
        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories) {
                // Симуляция на 30 шагов вперёд: зелёная (если не жать) и красная (если прыгнуть)
                CCPoint simNoClick = pos;
                CCPoint simClick = pos;
                float velNoClick = player->m_yVelocity;
                float velClick = 11.0f; // Базовый импульс прыжка куба

                for (int i = 0; i < 30; ++i) {
                    CCPoint prevNoClick = simNoClick;
                    CCPoint prevClick = simClick;

                    simNoClick.x += 5.2f;
                    simNoClick.y += velNoClick * 0.4f;
                    velNoClick -= 0.85f; // Гравитация

                    simClick.x += 5.2f;
                    simClick.y += velClick * 0.4f;
                    velClick -= 0.85f;

                    AutoBot::trajectoryNode->drawSegment(prevNoClick, simNoClick, 1.0f, { 0.f, 1.f, 0.f, 0.7f });
                    AutoBot::trajectoryNode->drawSegment(prevClick, simClick, 1.0f, { 1.f, 0.2f, 0.2f, 0.7f });
                }
            }
        }

        // Воспроизведение записанного пути (для шоукейса)
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