// Demon Bot – Menue-Autopilot: laedt alle bewerteten Classic-Demons nacheinander und spielt sie.
// Noclip und Speedhack kommen von Eclipse; dieser Mod gibt KEINE Eingaben, er bedient nur das Menue.
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string str(gd::string const& s) {
    return std::string(s.c_str(), s.size());
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    return s.substr(i);
}

std::string currentAccount() {
    auto am = GJAccountManager::get();
    return am ? str(am->m_username) : std::string();
}

// true = eingeloggt mit dem Account, auf dem der Bot nie laufen darf
bool isProtectedAccount() {
    auto prot = lower(trim(Mod::get()->getSettingValue<std::string>("protected-account")));
    if (prot.empty()) return false;
    return lower(trim(currentAccount())) == prot;
}

// Laeuft gerade ein Szenenwechsel oder ist ein Level offen?
bool sceneReady() {
    auto sc = CCDirector::get()->getRunningScene();
    if (!sc) return false;
    if (typeinfo_cast<CCTransitionScene*>(sc)) return false;
    auto children = sc->getChildren();
    if (!children) return true;
    for (unsigned i = 0; i < children->count(); i++) {
        if (typeinfo_cast<PlayLayer*>(children->objectAtIndex(i))) return false;
    }
    return true;
}

// Server-Suche: demonFilter 1..5 = Easy..Extreme, 0 = alle Demons
const char* FILTER_PLURAL[] = {"Alle Demons", "Easy Demons", "Medium Demons", "Hard Demons", "Insane Demons", "Extreme Demons"};
const char* FILTER_SINGLE[] = {"Demon", "Easy Demon", "Medium Demon", "Hard Demon", "Insane Demon", "Extreme Demon"};

struct Entry {
    int id = 0;
    std::string name;
    std::string creator;
    int raw = 0;            // Demon-Schwierigkeit laut Server (nur fuer den Filter-Selbsttest)
    bool platformer = false;
};

class DemonBot : public CCObject, public LevelManagerDelegate, public LevelDownloadDelegate {
public:
    enum class Ph { Idle, Page, WaitPage, Next, WaitDl, Play, Playing, Leaving, Backoff };

    bool running = false;
    bool showSummaryOnMenu = false;
    std::string summaryText;

    Ph ph = Ph::Idle;
    Ph afterBackoff = Ph::Idle;

    // Position in der Suche
    int pass = 1;           // 1 = Hauptdurchlauf, 2 = Nachlese (verpasste Level einsammeln)
    int filter = 1;         // 1..5
    bool allMode = false;   // Fallback, falls der Schwierigkeitsfilter nicht greift
    int page = 0;
    int total = -1;
    int pageSize = 10;
    bool lastPageShort = false;
    std::string pendingKey;
    std::vector<Entry> list;
    size_t idx = 0;

    // Zaehler
    int number = 0;         // gespielte Level in diesem Lauf (Anzeige "#123")
    int done = 0;
    int skipped = 0;
    int already = 0;
    std::string failedText;
    std::set<int> failedIds;

    // Server
    double tPhase = 0;
    double tLastReq = -1e9;
    double tBackoffUntil = 0;
    int retries = 0;
    int dlFailStreak = 0;
    int longBackoffs = 0;
    int sinceSave = 0;

    // aktuelles Level
    Ref<GJGameLevel> current;
    Ref<GJGameLevel> pendingDelete;
    int resets = 0;
    int uncountedStreak = 0;
    std::string stopAfterLeave;
    bool completed = false;
    bool endShown = false;
    double tCompleted = 0;
    double tEnd = 0;
    double tLevelStart = 0;
    double tProgressReal = 0;
    double progressGame = 0;
    float bestPct = 0;

    static DemonBot* get() {
        static DemonBot* inst = [] {
            auto bot = new DemonBot();   // lebt bis zum Spielende
            CCDirector::get()->getScheduler()->scheduleSelector(
                schedule_selector(DemonBot::tick), bot, 0.05f, false
            );
            return bot;
        }();
        return inst;
    }

    // ---------- Einstellungen ----------
    double gap() { return Mod::get()->getSettingValue<double>("request-gap"); }
    double maxReal() { return static_cast<double>(Mod::get()->getSettingValue<int64_t>("max-real-seconds")); }
    double maxGame() { return static_cast<double>(Mod::get()->getSettingValue<int64_t>("max-game-seconds")); }

    std::string filterLabel() const { return allMode ? FILTER_PLURAL[0] : FILTER_PLURAL[filter]; }
    std::string diffLabel() const { return allMode ? FILTER_SINGLE[0] : FILTER_SINGLE[filter]; }
    Entry const* entry() const { return idx < list.size() ? &list[idx] : nullptr; }

    // ---------- Speichern / Laden ----------
    void saveProgress() {
        auto m = Mod::get();
        m->setSavedValue<int>("pass", pass);
        m->setSavedValue<int>("filter", filter);
        m->setSavedValue<bool>("all-mode", allMode);
        m->setSavedValue<int>("page", page);
        m->setSavedValue<int>("number", number);
        m->setSavedValue<int>("done", done);
        m->setSavedValue<int>("skipped", skipped);
        m->setSavedValue<int>("already", already);
        m->setSavedValue<std::string>("failed-text", failedText);
        std::string ids;
        for (int id : failedIds) {
            if (!ids.empty()) ids += ",";
            ids += std::to_string(id);
        }
        m->setSavedValue<std::string>("failed-ids", ids);
        m->setSavedValue<bool>("has-progress", true);
    }

    void loadProgress() {
        auto m = Mod::get();
        pass = m->getSavedValue<int>("pass", 1);
        filter = m->getSavedValue<int>("filter", 1);
        allMode = m->getSavedValue<bool>("all-mode", false);
        page = m->getSavedValue<int>("page", 0);
        number = m->getSavedValue<int>("number", 0);
        done = m->getSavedValue<int>("done", 0);
        skipped = m->getSavedValue<int>("skipped", 0);
        already = m->getSavedValue<int>("already", 0);
        failedText = m->getSavedValue<std::string>("failed-text", "");
        failedIds.clear();
        std::stringstream ss(m->getSavedValue<std::string>("failed-ids", ""));
        std::string part;
        while (std::getline(ss, part, ',')) {
            try { if (!part.empty()) failedIds.insert(std::stoi(part)); } catch (...) {}
        }
        if (filter < 1 || filter > 5) filter = 1;
        if (page < 0) page = 0;
    }

    void resetProgress() {
        pass = 1; filter = 1; allMode = false; page = 0;
        number = 0; done = 0; skipped = 0; already = 0;
        failedText.clear(); failedIds.clear();
    }

    bool hasSavedProgress() { return Mod::get()->getSavedValue<bool>("has-progress", false); }

    std::string savedPosition() {
        auto m = Mod::get();
        int f = m->getSavedValue<int>("filter", 1);
        if (f < 1 || f > 5) f = 1;
        bool all = m->getSavedValue<bool>("all-mode", false);
        return fmt::format("{}, Seite {} (geschafft: {})",
            all ? FILTER_PLURAL[0] : FILTER_PLURAL[f], m->getSavedValue<int>("page", 0) + 1, m->getSavedValue<int>("done", 0));
    }

    // GD-Spielstand + Bot-Stand zusammen auf die Platte schreiben
    void saveAll() {
        sinceSave = 0;
        saveProgress();
        if (sceneReady()) AppDelegate::get()->trySaveGame(true);
        (void)Mod::get()->saveData();
    }

    void writeReport() {
        try {
            std::ofstream out(Mod::get()->getSaveDir() / "uebersprungen.txt");
            out << "Uebersprungene Level (Name von Ersteller (ID): Grund)\n\n" << failedText;
        } catch (...) {}
    }

    std::string summary(std::string const& reason) {
        return fmt::format(
            "{}\n\nGeschafft: <cg>{}</c>\nUebersprungen: <cr>{}</c>\nSchon vorher geschafft: {}\n\n"
            "Liste der uebersprungenen Level: uebersprungen.txt im Mod-Ordner.",
            reason, done, skipped, already
        );
    }

    // ---------- Start / Stopp ----------
    void start(bool resume) {
        if (running) return;
        if (isProtectedAccount()) return;
        if (resume) loadProgress(); else resetProgress();
        running = true;
        ph = Ph::Page;
        list.clear(); idx = 0; total = -1; pageSize = 10;
        retries = 0; dlFailStreak = 0; longBackoffs = 0; sinceSave = 0;
        uncountedStreak = 0; stopAfterLeave.clear(); lastPageShort = false;
        current = nullptr; pendingDelete = nullptr;
        Mod::get()->setSavedValue<bool>("running", true);
        saveProgress();
        (void)Mod::get()->saveData();
        log::info("Demon-Bot gestartet ({}), {} Seite {}", resume ? "weiter" : "neu", filterLabel(), page + 1);
        Notification::create("Demon-Bot gestartet", NotificationIcon::Success)->show();
    }

    void releaseDelegates() {
        auto glm = GameLevelManager::get();
        if (glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
        if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    }

    // Bot anhalten (Spieler bleibt, wo er ist)
    void stop(std::string const& reason) {
        if (!running) return;
        running = false;
        ph = Ph::Idle;
        releaseDelegates();
        Mod::get()->setSavedValue<bool>("running", false);
        writeReport();
        saveAll();
        log::info("Demon-Bot gestoppt: {}", reason);
        FLAlertLayer::create("Demon-Bot", summary(reason).c_str(), "OK")->show();
    }

    // Alles durch: zurueck ins Hauptmenue, dort Zusammenfassung zeigen
    void finish() {
        running = false;
        ph = Ph::Idle;
        releaseDelegates();
        Mod::get()->setSavedValue<bool>("running", false);
        Mod::get()->setSavedValue<bool>("has-progress", false);
        writeReport();
        saveAll();
        summaryText = summary("Fertig! Alle Demons sind durch.");
        showSummaryOnMenu = true;
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
    }

    void backoff(double seconds, Ph next) {
        tBackoffUntil = nowSec() + seconds;
        afterBackoff = next;
        ph = Ph::Backoff;
        log::info("Warte {} s", seconds);
    }

    // ---------- Ablauf ----------
    void tick(float) {
        if (!running) return;
        double t = nowSec();
        switch (ph) {
            case Ph::Backoff:
                if (t >= tBackoffUntil) ph = afterBackoff;
                break;
            case Ph::Page:
                requestPage(t);
                break;
            case Ph::WaitPage:
                if (t - tPhase > 25.0) { releaseDelegates(); pageFailed("keine Antwort"); }
                break;
            case Ph::Next:
                nextLevel(t);
                break;
            case Ph::WaitDl:
                if (t - tPhase > 25.0) { releaseDelegates(); downloadFailed("keine Antwort"); }
                break;
            case Ph::Play:
                startPlay(t);
                break;
            case Ph::Playing:
                watch(t);
                break;
            case Ph::Leaving:
                if (sceneReady()) {
                    if (!stopAfterLeave.empty()) {
                        auto why = stopAfterLeave;
                        stopAfterLeave.clear();
                        uncountedStreak = 0;
                        stop(why);
                        return;
                    }
                    ph = Ph::Next;
                }
                else if (t - tPhase > 20.0) stop("Das Level liess sich nicht verlassen.");
                break;
            default:
                break;
        }
    }

    void requestPage(double t) {
        if (t - tLastReq < gap()) return;
        auto so = GJSearchObject::create(
            SearchType::Recent, "", "-2", "-", page,
            true,   // nur bewertete Level
            false, false, 0, false, false, false, false, false, false, false, false, false, false,
            allMode ? 0 : filter, 0, 0
        );
        auto glm = GameLevelManager::get();
        std::string key = so->getKey();
        pendingKey = key;
        ph = Ph::WaitPage;
        tPhase = t;
        auto stored = pass == 1 ? glm->getStoredOnlineLevels(key.c_str()) : nullptr;
        if (stored) {
            if (stored->count() > 0) {
                if (auto info = glm->getPageInfo(key.c_str())) parsePageInfo(info);
                handlePage(stored);
                return;
            }
        }
        tLastReq = t;
        glm->m_levelManagerDelegate = this;
        glm->getOnlineLevels(so);
    }

    void parsePageInfo(std::string const& info) {
        int a = -1, b = 0, c = 10;
        if (std::sscanf(info.c_str(), "%d:%d:%d", &a, &b, &c) >= 1) {
            if (a >= 0) total = a;
            if (c > 0) pageSize = c;
        }
    }

    void handlePage(CCArray* levels) {
        retries = 0;
        list.clear();
        idx = 0;
        if (levels) {
            for (unsigned i = 0; i < levels->count(); i++) {
                auto lvl = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i));
                if (!lvl) continue;
                Entry e;
                e.id = lvl->m_levelID.value();
                e.name = str(lvl->m_levelName);
                e.creator = str(lvl->m_creatorName);
                e.raw = lvl->m_demonDifficulty;
                e.platformer = lvl->isPlatformer();
                list.push_back(std::move(e));
            }
        }
        lastPageShort = static_cast<int>(list.size()) < pageSize;
        log::info("{} Seite {}: {} Level (gesamt {})", filterLabel(), page + 1, list.size(), total);
        if (list.empty()) { endOfFilter(); return; }

        // Selbsttest: greift der Schwierigkeitsfilter? Dann haben (fast) alle Level dieselbe Demon-Stufe.
        if (!allMode && page == 0 && list.size() >= 3) {
            std::map<int, int> count;
            for (auto& e : list) count[e.raw]++;
            int best = 0;
            for (auto& [k, v] : count) best = std::max(best, v);
            if (best < static_cast<int>(list.size()) - 1) {
                log::warn("Demon-Filter greift nicht ({} von {} gleich) -> alle Demons gemischt", best, list.size());
                allMode = true;
                page = 0; total = -1; list.clear();
                ph = Ph::Page;
                Notification::create("Filter klappt nicht - spiele alle Demons gemischt", NotificationIcon::Warning, 4.f)->show();
                return;
            }
        }
        ph = Ph::Next;
    }

    void pageFailed(std::string const& why) {
        log::warn("Seite {} fehlgeschlagen: {}", page + 1, why);
        if (total >= 0 && page * pageSize >= total) { endOfFilter(); return; }
        retries++;
        if (retries <= 4) {
            static const double waits[] = {10, 30, 60, 120};
            backoff(waits[retries - 1], Ph::Page);
            return;
        }
        retries = 0;
        if (page == 0 && !allMode) {
            // Filterwert wird vom Server evtl. nicht angenommen
            allMode = true; total = -1;
            ph = Ph::Page;
            return;
        }
        if (page == 0) { stop("Der Server liefert keine Level (evtl. IP-Sperre). Spaeter weitermachen."); return; }
        // Mitten in der Liste: Rest dieser Stufe auslassen, die Nachlese holt Verpasstes
        endOfFilter();
    }

    void endOfFilter() {
        page = 0; total = -1; list.clear(); idx = 0;
        if (!allMode && filter < 5) {
            filter++;
            ph = Ph::Page;
        } else if (pass == 1) {
            pass = 2; filter = 1;
            ph = Ph::Page;
            Notification::create("Nachlese: suche verpasste Level", NotificationIcon::Info, 4.f)->show();
        } else {
            finish();
            return;
        }
        saveProgress();
    }

    void advancePage() {
        page++;
        list.clear(); idx = 0;
        if ((total >= 0 && page * pageSize >= total) || lastPageShort) endOfFilter();
        else ph = Ph::Page;
        saveProgress();
    }

    void nextLevel(double t) {
        if (!sceneReady()) return;
        auto gsm = GameStatsManager::get();
        while (idx < list.size()) {
            auto& e = list[idx];
            if (e.platformer || failedIds.count(e.id)) { idx++; continue; }
            if (gsm->hasCompletedOnlineLevel(e.id)) {
                if (pass == 1) already++;
                idx++;
                continue;
            }
            break;
        }
        if (idx >= list.size()) { advancePage(); return; }
        if (sinceSave >= 10) saveAll();
        if (t - tLastReq < gap()) return;

        auto glm = GameLevelManager::get();
        ph = Ph::WaitDl;
        tPhase = t;
        tLastReq = t;
        glm->m_levelDownloadDelegate = this;
        glm->downloadLevel(list[idx].id, false, 0);
    }

    void downloadFailed(std::string const& why) {
        log::warn("Download fehlgeschlagen: {}", why);
        retries++;
        if (retries <= 2) { backoff(10.0 * retries, Ph::Next); return; }
        retries = 0;
        dlFailStreak++;
        if (dlFailStreak >= 3) {
            // Mehrere Level hintereinander kaputt -> wahrscheinlich blockt der Server gerade
            dlFailStreak = 0;
            longBackoffs++;
            if (longBackoffs > 3) {
                stop("Der Server antwortet nicht mehr (evtl. IP-Sperre). Spaeter einfach weitermachen.");
                return;
            }
            Notification::create(fmt::format("Server blockt? Warte {} Minuten", 5 * longBackoffs), NotificationIcon::Warning, 5.f)->show();
            backoff(300.0 * longBackoffs, Ph::Next);
            return;
        }
        markSkipped("Download fehlgeschlagen (" + why + ")");
        idx++;
        ph = Ph::Next;
    }

    void markSkipped(std::string const& reason) {
        auto e = entry();
        if (!e) return;
        skipped++;
        failedIds.insert(e->id);
        failedText += fmt::format("{} von {} ({}): {}\n", e->name, e->creator, e->id, reason);
        log::info("Uebersprungen: {} ({}) - {}", e->name, e->id, reason);
    }

    void startPlay(double t) {
        if (!sceneReady()) return;
        if (!current) { ph = Ph::Next; return; }
        number++;
        resets = 0;
        completed = false;
        endShown = false;
        bestPct = 0;
        progressGame = 0;
        tLevelStart = t;
        tProgressReal = t;
        tPhase = t;
        ph = Ph::Playing;
        auto scene = PlayLayer::scene(current, false, false);
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.2f, scene));
    }

    void flushDelete() {
        if (pendingDelete && Mod::get()->getSettingValue<bool>("delete-after-play")) {
            GameLevelManager::get()->deleteLevel(pendingDelete);
        }
        pendingDelete = nullptr;
    }

    void leaveLevel(PlayLayer* pl, bool success, std::string const& reason) {
        if (success) {
            done++;
            auto e = entry();
            bool counted = e && GameStatsManager::get()->hasCompletedOnlineLevel(e->id);
            uncountedStreak = counted ? 0 : uncountedStreak + 1;
            if (uncountedStreak >= 3) {
                stopAfterLeave = "Die Level werden nicht als geschafft gespeichert. Ist der Safe Mode in Eclipse noch an?";
            }
        } else {
            markSkipped(reason);
        }
        idx++;
        sinceSave++;
        pendingDelete = current;
        current = nullptr;
        ph = Ph::Leaving;
        tPhase = nowSec();
        saveProgress();
        pl->onQuit();
    }

    void watch(double t) {
        auto pl = PlayLayer::get();
        if (!pl || pl->m_level != current.data()) {
            if (t - tPhase > 30.0) stop("Das Level ist nicht gestartet.");
            return;
        }
        // Vorheriges Level erst loeschen, wenn dessen Szene sicher weg ist
        if (pendingDelete && t - tPhase > 1.5 && !typeinfo_cast<CCTransitionScene*>(CCDirector::get()->getRunningScene())) {
            flushDelete();
        }
        if (completed) {
            if ((endShown && t - tEnd > 0.8) || t - tCompleted > 8.0) leaveLevel(pl, true, "");
            return;
        }
        if (resets >= 3) {
            stop("Der Spieler stirbt immer wieder. Ist Noclip in Eclipse an?");
            return;
        }
        double gt = pl->m_gameState.m_levelTime;
        float pct = pl->getCurrentPercent();
        if (gt < progressGame) progressGame = gt;  // Zeit wurde zurueckgesetzt
        if (pct > bestPct + 0.1f) {
            bestPct = pct;
            progressGame = gt;
            tProgressReal = t;
        }
        if (gt - progressGame > 45.0 && t - tProgressReal > 5.0) {
            leaveLevel(pl, false, fmt::format("haengt bei {}%", static_cast<int>(bestPct)));
            return;
        }
        if (gt > maxGame()) {
            leaveLevel(pl, false, fmt::format("laenger als {} Spielsekunden", static_cast<int>(maxGame())));
            return;
        }
        if (t - tLevelStart > maxReal()) {
            leaveLevel(pl, false, fmt::format("Zeitlimit bei {}%", static_cast<int>(bestPct)));
            return;
        }
    }

    // ---------- GD-Callbacks ----------
    void loadLevelsFinished(CCArray* levels, char const* key, int type) override {
        if (!running || ph != Ph::WaitPage) return;
        auto glm = GameLevelManager::get();
        // Nur unsere eigene Suche annehmen (falls noch eine andere Anfrage unterwegs war)
        auto mine = glm->getStoredOnlineLevels(pendingKey.c_str());
        bool sameKey = key && pendingKey == key;
        if (!sameKey && !(mine && mine->count() > 0)) {
            log::warn("Fremde Suchantwort ignoriert ({})", key ? key : "?");
            return;
        }
        glm->m_levelManagerDelegate = nullptr;
        if (auto info = glm->getPageInfo(pendingKey.c_str())) parsePageInfo(info);
        handlePage(sameKey ? levels : mine);
    }
    void loadLevelsFinished(CCArray* levels, char const* key) override {
        loadLevelsFinished(levels, key, 0);
    }
    void loadLevelsFailed(char const* key, int type) override {
        if (!running || ph != Ph::WaitPage) return;
        if (key && pendingKey != key) return;
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
        pageFailed("Server-Fehler oder leere Seite");
    }
    void loadLevelsFailed(char const* key) override {
        loadLevelsFailed(key, 0);
    }
    void setupPageInfo(gd::string info, char const* key) override {
        if (running && (!key || pendingKey == key)) parsePageInfo(str(info));
    }
    void levelDownloadFinished(GJGameLevel* level) override {
        if (!running || ph != Ph::WaitDl) return;
        auto e = entry();
        if (level && e && level->m_levelID.value() != e->id) return;  // fremder Download
        GameLevelManager::get()->m_levelDownloadDelegate = nullptr;
        retries = 0;
        dlFailStreak = 0;
        longBackoffs = 0;
        if (!level || level->m_levelString.empty()) {
            markSkipped("Leveldaten leer");
            idx++;
            ph = Ph::Next;
            return;
        }
        if (level->isPlatformer()) { idx++; ph = Ph::Next; return; }
        current = level;
        ph = Ph::Play;
    }
    void levelDownloadFailed(int response) override {
        if (!running || ph != Ph::WaitDl) return;
        GameLevelManager::get()->m_levelDownloadDelegate = nullptr;
        downloadFailed(fmt::format("Code {}", response));
    }

    // ---------- Hooks aus PlayLayer ----------
    bool controls(PlayLayer* pl) const {
        return running && ph == Ph::Playing && pl && pl->m_level == current.data();
    }
    void onLevelComplete(PlayLayer* pl) {
        if (!controls(pl) || completed) return;
        completed = true;
        tCompleted = nowSec();
    }
    void onEndLayer(PlayLayer* pl) {
        if (!controls(pl)) return;
        endShown = true;
        tEnd = nowSec();
    }
    void onReset(PlayLayer* pl) {
        if (controls(pl) && pl->m_gameState.m_levelTime > 1.0) resets++;
    }
};

CCSprite* makeButtonSprite() {
    const char* frame = "diffIcon_10_btn_001.png";
    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame)) frame = "GJ_playBtn2_001.png";
    return CircleButtonSprite::createWithSpriteFrameName(frame, 0.9f, CircleBaseColor::Red, CircleBaseSize::Medium);
}

} // namespace

// ---------- Hauptmenue: Knopf + Fortsetzen nach Absturz ----------
class $modify(BotMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto btn = CCMenuItemSpriteExtra::create(makeButtonSprite(), this, menu_selector(BotMenuLayer::onDemonBot));
        btn->setID("demon-bot-button"_spr);
        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto own = CCMenu::create();
            own->addChild(btn);
            auto win = CCDirector::get()->getWinSize();
            own->setPosition({win.width - 40.f, 40.f});
            own->setID("demon-bot-menu"_spr);
            this->addChild(own, 10);
        }

        // Nachrichten erst nach dem Einblenden zeigen
        this->runAction(CCSequence::create(
            CCDelayTime::create(1.0f),
            CCCallFunc::create(this, callfunc_selector(BotMenuLayer::afterEnter)),
            nullptr
        ));
        return true;
    }

    void afterEnter() {
        auto bot = DemonBot::get();
        if (bot->showSummaryOnMenu) {
            bot->showSummaryOnMenu = false;
            FLAlertLayer::create("Demon-Bot", bot->summaryText.c_str(), "OK")->show();
            return;
        }
        static bool askedResume = false;
        if (!askedResume && !bot->running && Mod::get()->getSavedValue<bool>("running", false)) {
            askedResume = true;
            if (isProtectedAccount()) return;
            createQuickPopup(
                "Demon-Bot",
                fmt::format("Der Bot wurde unterbrochen.\nStand: {}\n\nWeitermachen?", bot->savedPosition()),
                "Nein", "Weiter",
                [](FLAlertLayer*, bool weiter) {
                    if (weiter) DemonBot::get()->start(true);
                    else Mod::get()->setSavedValue<bool>("running", false);
                },
                true, true
            );
        }
    }

    void onDemonBot(CCObject*) {
        auto bot = DemonBot::get();
        if (bot->running) {
            createQuickPopup("Demon-Bot", "Der Bot laeuft gerade. Anhalten?", "Nein", "Anhalten",
                [](FLAlertLayer*, bool anhalten) {
                    if (anhalten) DemonBot::get()->stop("Von dir angehalten.");
                }, true, true);
            return;
        }
        if (isProtectedAccount()) {
            FLAlertLayer::create(
                "Gesperrt",
                fmt::format("Du bist mit <cr>{}</c> eingeloggt.\nDer Bot laeuft nur auf deinem Alt-Account.", currentAccount()).c_str(),
                "OK"
            )->show();
            return;
        }
        std::string info =
            "Vorher in Eclipse: <cg>Noclip</c> und <cg>Speedhack</c> an, <cr>Safe Mode</c> aus "
            "(sonst zaehlen die Level nicht).\nAnhalten: ESC / Pause.";
        if (bot->hasSavedProgress()) {
            createQuickPopup("Demon-Bot",
                fmt::format("{}\n\nGespeicherter Stand: {}", info, bot->savedPosition()),
                "Neu starten", "Weiter",
                [](FLAlertLayer*, bool weiter) { DemonBot::get()->start(weiter); },
                true, true);
        } else {
            createQuickPopup("Demon-Bot", info, "Abbrechen", "Start",
                [](FLAlertLayer*, bool go) { if (go) DemonBot::get()->start(false); },
                true, true);
        }
    }
};

// ---------- Level: Anzeige fuers Video, Ende erkennen, Auto-Pause verhindern ----------
class $modify(BotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto bot = DemonBot::get();
        auto e = bot->entry();
        if (bot->running && e && level == bot->current.data()) {
            auto node = CCNode::create();
            node->setID("overlay"_spr);

            auto l3 = CCLabelBMFont::create(fmt::format("Geschafft: {}   Skips: {}", bot->done, bot->skipped).c_str(), "bigFont.fnt");
            l3->setAnchorPoint({0.f, 0.f});
            l3->setScale(0.3f);
            l3->setPosition({6.f, 5.f});
            l3->setOpacity(200);

            auto l2 = CCLabelBMFont::create(fmt::format("by {}  |  {}", e->creator, bot->diffLabel()).c_str(), "bigFont.fnt");
            l2->setAnchorPoint({0.f, 0.f});
            l2->setScale(0.35f);
            l2->setPosition({6.f, 17.f});

            auto l1 = CCLabelBMFont::create(fmt::format("#{}  {}", bot->number, e->name).c_str(), "goldFont.fnt");
            l1->setAnchorPoint({0.f, 0.f});
            l1->setScale(0.6f);
            l1->setPosition({6.f, 30.f});

            node->addChild(l1);
            node->addChild(l2);
            node->addChild(l3);
            this->addChild(node, 1000);
        }
        return true;
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        DemonBot::get()->onLevelComplete(this);
    }

    void showEndLayer() {
        PlayLayer::showEndLayer();
        DemonBot::get()->onEndLayer(this);
    }

    void resetLevel() {
        DemonBot::get()->onReset(this);
        PlayLayer::resetLevel();
    }

    void pauseGame(bool unfocused) {
        auto bot = DemonBot::get();
        if (bot->controls(this)) {
            if (unfocused) return;               // im Hintergrund einfach weiterspielen
            bot->stop("Von dir pausiert.");      // ESC / Pause = Bot anhalten
        }
        PlayLayer::pauseGame(unfocused);
    }
};

// ---------- Stats nicht an die Bestenlisten senden ----------
class $modify(BotGLM, GameLevelManager) {
    void updateUserScore() {
        if (Mod::get()->getSettingValue<bool>("block-stats-upload") && !isProtectedAccount()) {
            log::info("Stats-Upload blockiert (Demon-Bot)");
            return;
        }
        GameLevelManager::updateUserScore();
    }
};
