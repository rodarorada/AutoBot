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
    int triedMask;   // Битовая маска уже опробованных веток (1 << branch)
    int activeBranch;// Какую ветку пробуем сейчас (-1 = авто)
};

namespace AutoBot {
    bool isRecording = false;
    bool isPlaying = false;
    bool showTrajectories = true;

    int currentFrame = 0;
    int graceFrames = 0; // Защита от случайного прыжка на старте/чекпоинте
    bool currentHold = false;
    std::vector<MacroAction> actions;
    std::vector<BotCheckpoint> checkpoints;
    CCDrawNode* trajectoryNode = nullptr;

    void setHold(GJBaseGameLayer* layer, bool hold) {
        if (currentHold != hold) {
            currentHold = hold;
            actions.push_back({ currentFrame, hold });
            layer->handleButton(hold, 1, true);
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

class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<GameObject*> nearbyObjects;
        bool isSimulating = false;
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
        AutoBot::graceFrames = 10; // Запрещаем прыжки в первые 10 кадров после старта/чекпоинта
        this->handleButton(false, 1, true);
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

        // Сохраняем полное состояние реального игрока перед прогоном ветки
        CCPoint origPos = player->getPosition();
        double origYVel = player->m_yVelocity;
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

        m_fields->isSimulating = true;
        const float stepDt = 1.0f / 60.0f;

        for (size_t step = 0; step < holdPattern.size(); ++step) {
            bool wantHold = holdPattern.at(step);
            if (wantHold && !player->m_isHolding) player->pushButton(PlayerButton::Jump);
            else if (!wantHold && player->m_isHolding) player->releaseButton(PlayerButton::Jump);

            // Прогоняем 4 нативных подшага физики GD (240 TPS)
            for (int sub = 0; sub < 4; ++sub) {
                player->update(stepDt / 4.0f);
                this->checkCollisions(player, stepDt / 4.0f, false);
                if (player->m_isDead) break;
            }

            CCPoint curPos = player->getPosition();
            res.points.push_back(curPos);
            if (player->m_isDead) {
                res.survived = false;
                break;
            }
            res.survivalSteps++;
        }

        // Возвращаем реального игрока в исходное состояние
        player->m_isDead = false;
        player->setPosition(origPos);
        player->m_yVelocity = origYVel;
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
        m_fields->isSimulating = false;

        return res;
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!this->m_player1 || this->m_player1->m_isDead || m_fields->isSimulating) return;

        AutoBot::currentFrame++;
        auto player = this->m_player1;
        CCPoint startPos = player->getPosition();
        collectNearbyObjects(startPos.x);

        const int steps = 85;
        std::vector<std::vector<bool>> patterns(6, std::vector<bool>(steps, false));
        std::fill(patterns.at(1).begin(), patterns.at(1).end(), true);
        for (int i = 0; i < 3; ++i) patterns.at(2).at(i) = true;
        for (int i = 6; i < 9; ++i) patterns.at(3).at(i) = true;
        for (int i = 14; i < 17; ++i) patterns.at(4).at(i) = true;
        for (int i = 0; i < 3; ++i) patterns.at(5).at(i) = true;
        for (int i = 20; i < 23; ++i) patterns.at(5).at(i) = true;

        bool isFlight = player->m_isShip || player->m_isDart;
        int branchCount = isFlight ? 2 : 6; // Для кораблика и волны только 0 (отпустить) и 1 (зажать)

        std::vector<BranchResult> results(branchCount);
        for (int i = 0; i < branchCount; ++i) results.at(i) = simulateBranch(player, patterns.at(i));

        if (AutoBot::trajectoryNode) {
            AutoBot::trajectoryNode->clear();
            if (AutoBot::showTrajectories) {
                std::vector<ccColor4F> colors = {
                    { 0.1f, 1.0f, 0.1f, 0.85f }, { 1.0f, 0.1f, 0.1f, 0.85f },
                    { 1.0f, 0.9f, 0.1f, 0.85f }, { 0.1f, 0.9f, 1.0f, 0.85f },
                    { 0.8f, 0.2f, 1.0f, 0.85f }, { 1.0f, 0.5f, 0.0f, 0.85f }
                };
                for (int i = 0; i < branchCount; ++i) {
                    CCPoint prev = startPos;
                    for (const auto& pt : results.at(i).points) {
                        AutoBot::trajectoryNode->drawSegment(prev, pt, 0.45f, colors.at(i));
                        prev = pt;
                    }
                }
                // Отрисовка хитбоксов игрока и ближайших объектов при включённом Show path
                CCRect pRect = player->getObjectRect();
                AutoBot::trajectoryNode->drawRect(pRect.origin, pRect.origin + pRect.size, {0,0,0,0}, 0.5f, {0.f, 1.f, 0.f, 0.9f});
                for (GameObject* obj : m_fields->nearbyObjects) {
                    if (obj->m_objectType == GameObjectType::Decoration) continue;
                    CCRect r = obj->getObjectRect();
                    ccColor4F c = (obj->m_objectType == GameObjectType::Hazard) ? ccColor4F{1.f, 0.f, 0.f, 0.7f} : ccColor4F{0.2f, 0.6f, 1.f, 0.5f};
                    AutoBot::trajectoryNode->drawRect(r.origin, r.origin + r.size, {0,0,0,0}, 0.4f, c);
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
                    int penalty = (mask & (1 << idx)) ? 40 : 0; // Штрафуем ветку, на которой уже умирали
                    if (!isFlight && idx != 0 && results.at(0).survivalSteps > 16 && !(mask & 1)) penalty += 50;
                    int score = results.at(idx).survivalSteps - penalty;
                    if (score > bestScore) { bestScore = score; chosen = idx; }
                }
                if (!AutoBot::checkpoints.empty()) AutoBot::checkpoints.back().activeBranch = chosen;
                AutoBot::setHold(this, patterns.at(chosen).at(0));
            }
        }

        if (AutoBot::isPlaying) {
            for (const auto& act : AutoBot::actions) {
                if (act.frame == AutoBot::currentFrame) this->handleButton(act.hold, 1, true);
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (m_fields->isSimulating) { player->m_isDead = true; return; }
        if (AutoBot::isRecording && !AutoBot::checkpoints.empty()) {
            auto& cp = AutoBot::checkpoints.back();
            if (cp.activeBranch != -1) cp.triedMask |= (1 << cp.activeBranch); // Запоминаем неудачное действие
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (AutoBot::isRecording) { AutoBot::isRecording = false; AutoBot::saveMacro(this->m_level->m_levelName); }
    }
};