// Demon Bot – Menue-Autopilot: laedt alle bewerteten Classic-Demons nacheinander und spielt sie.
// Noclip und Speedhack kommen von Eclipse; dieser Mod gibt KEINE Eingaben, er bedient nur das Menue.
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameStatsManager.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <unordered_map>
#include <cmath>
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

// Kategorien in Spielreihenfolge. diff = Server-Filter ("-3" Auto, "1".."5" Easy..Insane, "-2" Demon),
// demonFilter 1..5 = Easy..Extreme Demon
struct Cat {
    const char* plural;
    const char* single;
    const char* diff;
    int demonFilter;
};
const Cat CATS[] = {
    {"Auto-Level",     "Auto",          "-3", 0},
    {"Easy-Level",     "Easy",          "1",  0},
    {"Normal-Level",   "Normal",        "2",  0},
    {"Hard-Level",     "Hard",          "3",  0},
    {"Harder-Level",   "Harder",        "4",  0},
    {"Insane-Level",   "Insane",        "5",  0},
    {"Easy Demons",    "Easy Demon",    "-2", 1},
    {"Medium Demons",  "Medium Demon",  "-2", 2},
    {"Hard Demons",    "Hard Demon",    "-2", 3},
    {"Insane Demons",  "Insane Demon",  "-2", 4},
    {"Extreme Demons", "Extreme Demon", "-2", 5},
};
constexpr int CAT_COUNT = 11;                       // Schwierigkeiten
constexpr int FIRST_DEMON = 6;
constexpr int TYPE_COUNT = 2;                       // 0 = Classic, 1 = Platformer
constexpr int ALL_CATS = CAT_COUNT * TYPE_COUNT;    // Kategorie c: Typ c / 11, Schwierigkeit c % 11
constexpr int DIFF_BITS = (1 << CAT_COUNT) - 1;     // Bits 0..10: Schwierigkeiten
constexpr int TYPE_CLASSIC = 1 << CAT_COUNT;        // Bit 11
constexpr int TYPE_PLAT = 1 << (CAT_COUNT + 1);     // Bit 12
constexpr int MASK_ALL = DIFF_BITS;                 // alle Schwierigkeiten
constexpr int MASK_DEMONS = DIFF_BITS & ~((1 << FIRST_DEMON) - 1);
int catDiff(int c) { return c % CAT_COUNT; }
int catType(int c) { return c / CAT_COUNT; }
// Gueltige Maske: mindestens ein Level-Typ (alte Staende kannten nur Classic)
int fixMask(int m) {
    m &= DIFF_BITS | TYPE_CLASSIC | TYPE_PLAT;
    if (!(m & (TYPE_CLASSIC | TYPE_PLAT))) m |= TYPE_CLASSIC;
    return m;
}
// Demon-Stufe laut Server (Level-Key 43) -> Kategorie; nur fuer den Notfall-Modus "Demons gemischt"
int rawToCat(int raw) {
    switch (raw) {
        case 3: return 6; case 4: return 7; case 0: return 8; case 5: return 9; case 6: return 10;
        default: return -1;
    }
}

int clampCat(int c) { return c < 0 || c >= ALL_CATS ? FIRST_DEMON : c; }
bool diffInMask(int mask, int d) { return d >= 0 && d < CAT_COUNT && (mask >> d & 1); }
bool inMask(int mask, int c) {
    return c >= 0 && c < ALL_CATS && diffInMask(mask, catDiff(c)) && (mask >> (CAT_COUNT + catType(c)) & 1);
}
int nextInMask(int mask, int after) {
    for (int c = after + 1; c < ALL_CATS; c++) if (inMask(mask, c)) return c;
    return -1;
}

std::string catLabel(int cat, bool demonAll) {
    cat = clampCat(cat);
    std::string base = (catDiff(cat) >= FIRST_DEMON && demonAll) ? "Demons (gemischt)" : CATS[catDiff(cat)].plural;
    return catType(cat) == 1 ? base + " (Platformer)" : base;
}

std::string maskLabel(int mask) {
    mask = fixMask(mask);
    std::string types = (mask & TYPE_CLASSIC) && (mask & TYPE_PLAT) ? "Classic + Platformer"
                      : (mask & TYPE_PLAT) ? "Platformer" : "Classic";
    int d = mask & DIFF_BITS;
    if (d == MASK_ALL) return types + ": alle Schwierigkeiten";
    if (d == MASK_DEMONS) return types + ": nur Demons";
    std::string out;
    int n = 0;
    for (int c = 0; c < CAT_COUNT; c++) {
        if (!diffInMask(d, c)) continue;
        if (++n > 3) { out += ", ..."; break; }
        if (!out.empty()) out += ", ";
        out += CATS[c].single;
    }
    return types + ": " + (out.empty() ? "nichts" : out);
}

// Alter Stand (v1.0 / v1.1) -> Auswahl-Maske
int legacyMask() {
    return (Mod::get()->getSavedValue<int>("mode", 0) == 1 ? MASK_ALL : MASK_DEMONS) | TYPE_CLASSIC;
}

// =====================================================================
// Platformer-Bot: findet mit dem echten Spiel selbst einen Weg zum Ziel.
//
// Er speichert Spielzustaende (wie Platformer-Checkpoints), probiert von dort aus zufaellige
// Lauf-/Sprung-Folgen aus und merkt sich jeden neu erreichten Ort ("Go-Explore"). Ist das Ziel
// gefunden, wird der Weg einmal sauber von vorne gespielt, damit das Level normal geschafft wird.
// =====================================================================
constexpr float STEP_DT = 1.f / 240.f;
constexpr int B_JUMP = 1, B_LEFT = 2, B_RIGHT = 3;

struct Ev {
    int tick;
    int btn;
    bool down;
};

struct Node {
    Ref<CheckpointObject> cp;
    std::shared_ptr<Node> parent;   // Zustand, von dem aus die Episode gestartet ist
    std::vector<Ev> seg;            // Eingaben ab dem Laden von parent (Ticks relativ dazu)
    int segTicks = 0;
    int total = 0;
    float x = 0.f, y = 0.f;
    int chosen = 0;
    int found = 0;
    bool inSection = false;
};
using NodeP = std::shared_ptr<Node>;

struct Segment {
    NodeP from;
    std::vector<Ev> evs;
    int ticks = 0;
};

class PBot {
public:
    enum class Mode { Off, Search, Final };

    static PBot& get() {
        static PBot bot;
        return bot;
    }

    Mode mode = Mode::Off;
    bool active() const { return mode != Mode::Off; }
    bool searching() const { return mode == Mode::Search; }

    PlayLayer* pl = nullptr;
    bool inStep = false;
    bool inRestore = false;
    bool inCheckpointActivated = false;
    bool botInput = false;
    bool dead = false;
    bool reached = false;
    std::string failReason;

    NodeP root;
    NodeP best;
    std::unordered_map<int64_t, NodeP> cells;
    std::mt19937 rng{1234567u};
    double tStart = 0;
    double tInfo = 0;
    long long steps = 0;
    int episodes = 0;
    float minX = 0, maxX = 0;

    struct Episode {
        NodeP start;
        bool running = false;
        int tick = 0;
        int maxTicks = 0;
        std::vector<Ev> evs;
        int h = 0;
        int actLeft = 0;
        int jumpPress = -1;
        int jumpRelease = -1;
        bool held[4] = {false, false, false, false};
        float lastX = 0, lastY = 0;
        int still = 0;
    } ep;

    // Sauberer Lauf am Ende
    std::vector<Segment> plan;
    std::vector<Ev> flat;
    int flatTicks = 0;
    bool segmentedFinal = false;
    size_t fIdx = 0;
    int fTick = 0;
    size_t segIdx = 0;
    size_t evIdx = 0;
    double acc = 0;

    std::vector<Ref<GameObject>> dummies;   // unsichtbare Checkpoint-Objekte
    float savedMusic = -1.f, savedSfx = -1.f;
    Ref<CCNode> overlay;
    CCLabelBMFont* infoLabel = nullptr;

    static int maxCells() { return static_cast<int>(Mod::get()->getSettingValue<int64_t>("plat-max-cells")); }
    static double maxSearchSec() { return 60.0 * Mod::get()->getSettingValue<int64_t>("plat-search-minutes"); }
    static double budget() { return Mod::get()->getSettingValue<int64_t>("plat-frame-budget-ms") / 1000.0; }

    // ---------- Start / Stopp ----------
    void begin(PlayLayer* layer) {
        if (active() || !layer) return;
        pl = layer;
        failReason.clear();
        auto fm = FMODAudioEngine::sharedEngine();
        savedMusic = fm->m_musicVolume;
        savedSfx = fm->m_sfxVolume;
        fm->setBackgroundMusicVolume(0.f);
        fm->setEffectsVolume(0.f);

        // Sauberer Start: Level von vorne, dieser Zustand ist die Wurzel der Suche
        inRestore = true;
        botInput = true;
        pl->resetLevelFromStart();
        botInput = false;
        inRestore = false;
        releaseAll();

        cells.clear();
        plan.clear();
        flat.clear();
        steps = 0;
        episodes = 0;
        ep = Episode{};
        dead = reached = false;
        root = makeNode(nullptr, {}, 0);
        if (!root) {
            fail("Spielzustand liess sich nicht speichern");
            return;
        }
        cells[cellKey()] = root;
        best = root;
        minX = maxX = root->x;
        mode = Mode::Search;
        tStart = nowSec();
        tInfo = 0;
        buildOverlay();
        log::info("Platformer-Bot sucht ...");
    }

    void restoreSound() {
        if (savedMusic >= 0.f) {
            auto fm = FMODAudioEngine::sharedEngine();
            fm->setBackgroundMusicVolume(savedMusic);
            fm->setEffectsVolume(savedSfx);
            savedMusic = savedSfx = -1.f;
        }
    }

    void clearSearch() {
        cells.clear();
        plan.clear();
        flat.clear();
        ep = Episode{};
        best = nullptr;
        root = nullptr;
    }

    // Abbruch mit Grund (der Demon-Bot ueberspringt dann das Level)
    void fail(std::string const& why) {
        log::info("Platformer-Bot: {}", why);
        failReason = why;
        if (pl && mode != Mode::Off) releaseAll();
        mode = Mode::Off;
        removeOverlay();
        restoreSound();
        clearSearch();
    }

    // Ziel erreicht: ab hier laeuft das Spiel wieder normal (Abschluss-Animation usw.)
    void handover() {
        mode = Mode::Off;
        removeOverlay();
        restoreSound();
        clearSearch();
    }

    void shutdown(bool playLayerAlive) {
        if (playLayerAlive && pl && mode != Mode::Off) releaseAll();
        mode = Mode::Off;
        removeOverlay();
        restoreSound();
        clearSearch();
        failReason.clear();
        inStep = inRestore = botInput = inCheckpointActivated = false;
        if (!playLayerAlive) {
            dummies.clear();
            pl = nullptr;
        }
    }

    // ---------- Spielzustaende ----------
    NodeP makeNode(NodeP parent, std::vector<Ev> const& evs, int segTicks) {
        auto cp = pl->createCheckpoint();
        if (!cp) return nullptr;
        auto pos = pl->m_player1->getPosition();
        // Unsichtbares Ersatz-Objekt, wie bei Practice-Checkpoints (das Spiel erwartet eins)
        auto obj = GameObject::createWithFrame("square_01_001.png");
        obj->m_objectID = 44;
        obj->m_objectType = GameObjectType::Decoration;
        obj->m_glowSprite = nullptr;
        obj->m_isDisabled2 = true;
        obj->m_isInvisible = true;
        obj->setOpacity(0);
        obj->setStartPos(pos);
        obj->retain();
        CC_SAFE_RELEASE(cp->m_physicalCheckpointObject);
        cp->m_physicalCheckpointObject = obj;

        auto n = std::make_shared<Node>();
        n->cp = cp;
        n->parent = parent;
        n->seg = evs;
        n->segTicks = segTicks;
        n->total = (parent ? parent->total : 0) + segTicks;
        n->x = pos.x;
        n->y = pos.y;
        return n;
    }

    void ensureInSection(NodeP const& n) {
        if (n->inSection) return;
        auto obj = n->cp->m_physicalCheckpointObject;
        if (!obj) return;
        pl->addToSection(obj);
        obj->activateObject();
        n->inSection = true;
        dummies.push_back(obj);
    }

    void releaseAll() {
        botInput = true;
        for (int b = 1; b <= 3; b++) pl->handleButton(false, b, true);
        botInput = false;
    }

    void restore(NodeP const& n) {
        ensureInSection(n);
        inRestore = true;
        botInput = true;
        pl->m_checkpointArray->removeAllObjects();
        pl->m_checkpointArray->addObject(n->cp);
        pl->resetLevel();
        botInput = false;
        inRestore = false;
        releaseAll();
        dead = false;
        reached = false;
    }

    void step() {
        inStep = true;
        pl->update(STEP_DT);
        inStep = false;
        steps++;
    }

    int64_t cellKey() const {
        auto p = pl->m_player1;
        int cx = static_cast<int>(std::floor(p->getPositionX() / 24.f));
        int cy = static_cast<int>(std::floor(p->getPositionY() / 24.f));
        int m = p->m_isShip ? 1 : p->m_isBall ? 2 : p->m_isBird ? 3 : p->m_isDart ? 4
              : p->m_isRobot ? 5 : p->m_isSpider ? 6 : p->m_isSwing ? 7 : 0;
        int up = p->m_isUpsideDown ? 1 : 0;
        return ((static_cast<int64_t>(cx) + 1000000) * 4000000 + (static_cast<int64_t>(cy) + 1000000)) * 16 + m * 2 + up;
    }

    // ---------- Suche ----------
    double rnd() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }
    int rndInt(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

    NodeP pickNode() {
        std::vector<NodeP> nodes;
        std::vector<double> w;
        nodes.reserve(cells.size());
        w.reserve(cells.size());
        float range = std::max(1.f, maxX - minX);
        for (auto& [k, n] : cells) {
            double prog = (n->x - minX) / range;
            nodes.push_back(n);
            w.push_back(1.0 / std::sqrt(1.0 + n->chosen) * (1.0 + 0.8 * prog) + 0.3 * n->found / (1.0 + n->chosen));
        }
        if (nodes.empty()) return root;
        std::discrete_distribution<size_t> d(w.begin(), w.end());
        return nodes[d(rng)];
    }

    bool evictOne() {
        int64_t worstKey = 0;
        double worst = -1e18;
        for (auto& [k, n] : cells) {
            if (n == root || n == ep.start || n == best || n->chosen < 3) continue;
            double score = n->chosen - 3.0 * n->found;
            if (score > worst) { worst = score; worstKey = k; }
        }
        if (worst <= -1e17) return false;
        cells.erase(worstKey);
        return true;
    }

    void setHeld(int b, bool want) {
        if (ep.held[b] == want) return;
        botInput = true;
        pl->handleButton(want, b, true);
        botInput = false;
        ep.evs.push_back({ep.tick, b, want});
        ep.held[b] = want;
    }

    void chooseAction() {
        if (!(ep.tick > 0 && rnd() < 0.6)) {
            double r = rnd();
            ep.h = r < 0.5 ? 1 : (r < 0.78 ? -1 : 0);
        }
        double j = rnd();
        if (j < 0.35) {
            ep.jumpPress = -1;
        } else if (j < 0.6) {
            ep.jumpPress = ep.tick;
            ep.jumpRelease = ep.tick + rndInt(2, 8);
        } else {
            ep.jumpPress = ep.tick;
            ep.jumpRelease = ep.tick + rndInt(12, 60);
        }
        ep.actLeft = rndInt(8, 48);
    }

    void startEpisode() {
        NodeP n = pickNode();
        n->chosen++;
        restore(n);
        ep = Episode{};
        ep.start = n;
        ep.running = true;
        ep.maxTicks = rndInt(240 * 2, 240 * 6);
        ep.lastX = pl->m_player1->getPositionX();
        ep.lastY = pl->m_player1->getPositionY();
        episodes++;
    }

    void noteBounds(NodeP const& n) {
        minX = std::min(minX, n->x);
        maxX = std::max(maxX, n->x);
        if (!best || n->x > best->x) best = n;
    }

    void considerCell() {
        int64_t key = cellKey();
        int total = ep.start->total + ep.tick;
        auto it = cells.find(key);
        if (it == cells.end()) {
            if (static_cast<int>(cells.size()) >= maxCells() && !evictOne()) return;
            auto n = makeNode(ep.start, ep.evs, ep.tick);
            if (!n) return;
            cells[key] = n;
            ep.start->found++;
            noteBounds(n);
        } else if (total + 48 < it->second->total) {
            auto n = makeNode(ep.start, ep.evs, ep.tick);
            if (!n) return;
            n->chosen = it->second->chosen;
            n->found = it->second->found;
            if (it->second == best) best = n;
            it->second = n;
            noteBounds(n);
        }
    }

    void searchTick() {
        if (!ep.running) {
            startEpisode();
            return;
        }
        if (ep.actLeft <= 0) chooseAction();
        setHeld(B_LEFT, ep.h < 0);
        setHeld(B_RIGHT, ep.h > 0);
        if (ep.tick == ep.jumpRelease) setHeld(B_JUMP, false);
        if (ep.tick == ep.jumpPress) {
            if (ep.held[B_JUMP]) {
                setHeld(B_JUMP, false);   // erst loslassen, im naechsten Tick neu druecken
                ep.jumpPress = ep.tick + 1;
                if (ep.jumpRelease <= ep.jumpPress) ep.jumpRelease = ep.jumpPress + 3;
            } else {
                setHeld(B_JUMP, true);
            }
        }
        step();
        ep.tick++;
        ep.actLeft--;
        if (reached) {
            onGoalFound();
            return;
        }
        if (dead) {
            ep.running = false;
            return;
        }
        float x = pl->m_player1->getPositionX();
        float y = pl->m_player1->getPositionY();
        if (std::abs(x - ep.lastX) < 2.f && std::abs(y - ep.lastY) < 2.f) {
            ep.still++;
        } else {
            ep.still = 0;
            ep.lastX = x;
            ep.lastY = y;
        }
        considerCell();
        if (ep.tick >= ep.maxTicks || ep.still > 360) ep.running = false;
    }

    void onGoalFound() {
        log::info("Weg gefunden: {} Versuche, {} Schritte", episodes, steps);
        plan.clear();
        std::vector<NodeP> chain;
        for (NodeP n = ep.start; n; n = n->parent) chain.push_back(n);
        std::reverse(chain.begin(), chain.end());
        for (size_t i = 1; i < chain.size(); i++) plan.push_back({chain[i - 1], chain[i]->seg, chain[i]->segTicks});
        plan.push_back({ep.start, ep.evs, ep.tick});
        ep.running = false;
        startFinal();
    }

    // ---------- Sauberer Lauf ----------
    // Erst am Stueck von vorne (normaler Durchlauf); klappt das nicht, in Abschnitten.
    void startFinal() {
        flat.clear();
        int off = 0;
        for (size_t k = 0; k < plan.size(); k++) {
            if (k > 0) {
                for (int b = 1; b <= 3; b++) flat.push_back({off, b, false});
            }
            for (auto const& e : plan[k].evs) flat.push_back({off + e.tick, e.btn, e.down});
            off += plan[k].ticks;
        }
        flatTicks = off;
        removeOverlay();
        mode = Mode::Final;
        segmentedFinal = false;
        fIdx = 0;
        fTick = 0;
        acc = 0;
        inRestore = true;
        botInput = true;
        pl->resetLevelFromStart();
        botInput = false;
        inRestore = false;
        releaseAll();
        dead = reached = false;
    }

    void startSegmentedFinal() {
        log::info("Lauf am Stueck weicht ab -> Abschnitte");
        segmentedFinal = true;
        segIdx = 0;
        evIdx = 0;
        fTick = 0;
    }

    // false = Lauf (vorerst) vorbei
    bool finalTick() {
        if (!segmentedFinal) {
            while (fIdx < flat.size() && flat[fIdx].tick <= fTick) {
                botInput = true;
                pl->handleButton(flat[fIdx].down, flat[fIdx].btn, true);
                botInput = false;
                fIdx++;
            }
            step();
            fTick++;
            if (mode != Mode::Final) return false;   // Ziel erreicht -> Spiel uebernimmt
            if (dead || fTick > flatTicks + 600) startSegmentedFinal();
            return true;
        }
        if (segIdx >= plan.size()) {
            fail("Weg funktioniert beim Nachspielen nicht");
            return false;
        }
        auto& s = plan[segIdx];
        if (fTick == 0) {
            restore(s.from);
            evIdx = 0;
        }
        while (evIdx < s.evs.size() && s.evs[evIdx].tick <= fTick) {
            botInput = true;
            pl->handleButton(s.evs[evIdx].down, s.evs[evIdx].btn, true);
            botInput = false;
            evIdx++;
        }
        step();
        fTick++;
        if (mode != Mode::Final) return false;
        if (dead) {
            fail("Weg funktioniert beim Nachspielen nicht");
            return false;
        }
        if (fTick >= s.ticks) {
            segIdx++;
            fTick = 0;
        }
        return true;
    }

    // ---------- Pro Bild (statt des normalen Spiel-Updates) ----------
    void frame(float dt) {
        if (pl->m_isPaused) return;
        double t0 = nowSec();
        if (mode == Mode::Search) {
            double limit = budget();
            while (mode == Mode::Search && nowSec() - t0 < limit) searchTick();
            if (mode != Mode::Search) return;
            double el = t0 - tStart;
            if (el > maxSearchSec()) {
                fail("kein Weg gefunden (Zeitlimit)");
            } else if (t0 - tInfo > 0.25 && infoLabel && root) {
                tInfo = t0;
                infoLabel->setString(fmt::format(
                    "Zeit: {}:{:02}   Versuche: {}\nOrte: {}   Weitester Punkt: {:.0f} Bloecke",
                    static_cast<int>(el) / 60, static_cast<int>(el) % 60, episodes, cells.size(),
                    (maxX - root->x) / 30.f).c_str());
            }
        } else if (mode == Mode::Final) {
            acc += dt * 240.0;
            int n = std::min(static_cast<int>(acc), 3000);
            acc -= n;
            for (int i = 0; i < n && mode == Mode::Final; i++) {
                if (!finalTick()) break;
            }
        }
    }

    // ---------- Anzeige ----------
    void removeOverlay() {
        if (overlay) overlay->removeFromParent();
        overlay = nullptr;
        infoLabel = nullptr;
    }

    void buildOverlay() {
        removeOverlay();
        auto win = CCDirector::get()->getWinSize();
        auto layer = CCLayerColor::create({0, 0, 0, 215}, win.width, win.height);
        layer->setID("platformer-search"_spr);

        auto title = CCLabelBMFont::create("Bot sucht einen Weg ...", "goldFont.fnt");
        title->setScale(0.8f);
        title->setPosition({win.width / 2, win.height / 2 + 40.f});
        layer->addChild(title);

        infoLabel = CCLabelBMFont::create("Starte ...", "bigFont.fnt");
        infoLabel->setScale(0.35f);
        infoLabel->setAlignment(kCCTextAlignmentCenter);
        infoLabel->setPosition({win.width / 2, win.height / 2 + 2.f});
        layer->addChild(infoLabel);

        pl->addChild(layer, 900);   // unter der Video-Anzeige (z 1000)
        overlay = layer;
    }
};

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
    int mask = MASK_DEMONS | TYPE_CLASSIC; // Auswahl: Bits 0..10 Schwierigkeiten, Bit 11 Classic, Bit 12 Platformer
    int cat = FIRST_DEMON;  // aktuelle Kategorie (Index in CATS)
    bool demonAll = false;  // Fallback, falls der Demon-Filter nicht greift: alle Demons gemischt
    bool platLenFallback = false; // Fallback, falls der Server den Platformer-Filter nicht annimmt
    bool pbotStarted = false;     // Platformer-Bot fuer das aktuelle Level schon gestartet
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

    int firstCat() const { return nextInMask(mask, -1); }
    // erste ausgewaehlte Demon-Kategorie im aktuellen Level-Typ
    int firstDemonCat() const {
        int base = catType(cat) * CAT_COUNT;
        for (int c = base + FIRST_DEMON; c < base + CAT_COUNT; c++) if (inMask(mask, c)) return c;
        return cat;
    }
    bool allDemonsSelected() const { return (mask & MASK_DEMONS) == MASK_DEMONS; }
    bool isDemonCat() const { return catDiff(cat) >= FIRST_DEMON; }
    bool isPlatCat() const { return catType(cat) == 1; }
    std::string filterLabel() const { return catLabel(cat, demonAll); }
    std::string diffLabel() const {
        std::string d;
        if (isDemonCat() && demonAll) {
            auto e = entry();
            int c = e ? rawToCat(e->raw) : -1;
            d = c >= 0 ? CATS[c].single : "Demon";
        } else {
            d = CATS[catDiff(clampCat(cat))].single;
        }
        return isPlatCat() ? d + "  |  Platformer" : d;
    }
    Entry const* entry() const { return idx < list.size() ? &list[idx] : nullptr; }

    // ---------- Speichern / Laden ----------
    void saveProgress() {
        auto m = Mod::get();
        m->setSavedValue<int>("pass", pass);
        m->setSavedValue<int>("mask", mask);
        m->setSavedValue<int>("cat", cat);
        m->setSavedValue<bool>("demon-all", demonAll);
        m->setSavedValue<bool>("plat-len-fallback", platLenFallback);
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
        mask = m->getSavedValue<int>("mask", -1);
        if (mask < 0) mask = legacyMask();
        mask = fixMask(mask);
        if ((mask & DIFF_BITS) == 0) mask |= MASK_DEMONS;
        cat = m->getSavedValue<int>("cat", -1);
        demonAll = m->getSavedValue<bool>("demon-all", false);
        platLenFallback = m->getSavedValue<bool>("plat-len-fallback", false);
        if (cat < 0) {
            // Stand aus Version 1.0 (nur Demons) uebernehmen
            int f = m->getSavedValue<int>("filter", 1);
            cat = FIRST_DEMON + std::clamp(f, 1, 5) - 1;
            demonAll = m->getSavedValue<bool>("all-mode", false);
            mask = MASK_DEMONS | TYPE_CLASSIC;
        }
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
        cat = clampCat(cat);
        if (!inMask(mask, cat)) {
            int n = nextInMask(mask, cat);
            cat = n >= 0 ? n : firstCat();
        }
        if (page < 0) page = 0;
    }

    void resetProgress() {
        pass = 1; demonAll = false; platLenFallback = false; page = 0;
        number = 0; done = 0; skipped = 0; already = 0;
        failedText.clear(); failedIds.clear();
    }

    bool hasSavedProgress() { return Mod::get()->getSavedValue<bool>("has-progress", false); }

    std::string savedPosition() {
        auto m = Mod::get();
        int c = m->getSavedValue<int>("cat", -1);
        bool all = m->getSavedValue<bool>("demon-all", false);
        int mk = m->getSavedValue<int>("mask", -1);
        if (mk < 0) mk = legacyMask();
        if (c < 0) {
            c = FIRST_DEMON + std::clamp(m->getSavedValue<int>("filter", 1), 1, 5) - 1;
            all = m->getSavedValue<bool>("all-mode", false);
            mk = MASK_DEMONS | TYPE_CLASSIC;
        }
        return fmt::format("{}\nGerade: {}, Seite {} (geschafft: {})",
            maskLabel(mk), catLabel(c, all), m->getSavedValue<int>("page", 0) + 1, m->getSavedValue<int>("done", 0));
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
    void start(bool resume, int newMask = MASK_DEMONS | TYPE_CLASSIC) {
        if (running) return;
        if (isProtectedAccount()) return;
        if (resume) {
            loadProgress();
        } else {
            resetProgress();
            mask = fixMask(newMask);
            if ((mask & DIFF_BITS) == 0) mask |= MASK_DEMONS;
            cat = firstCat();
        }
        running = true;
        ph = Ph::Page;
        list.clear(); idx = 0; total = -1; pageSize = 10;
        retries = 0; dlFailStreak = 0; longBackoffs = 0; sinceSave = 0;
        uncountedStreak = 0; stopAfterLeave.clear(); lastPageShort = false;
        current = nullptr; pendingDelete = nullptr;
        Mod::get()->setSavedValue<bool>("running", true);
        saveProgress();
        (void)Mod::get()->saveData();
        log::info("Bot gestartet ({}, {}), {} Seite {}", resume ? "weiter" : "neu", maskLabel(mask), filterLabel(), page + 1);
        Notification::create(fmt::format("Bot gestartet: {}", maskLabel(mask)), NotificationIcon::Success)->show();
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
        if (PBot::get().active()) PBot::get().shutdown(true);
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
        summaryText = summary("Fertig! Alles aus deiner Auswahl ist durch.");
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
            SearchType::Recent, "", CATS[catDiff(cat)].diff, isPlatCat() && !platLenFallback ? "5" : "-", page,
            true,   // nur bewertete Level
            false, false, 0, false, false, false, false, false, false, false, false, false, false,
            isDemonCat() && !demonAll ? CATS[catDiff(cat)].demonFilter : 0, 0, 0
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

        // Selbsttest: liefert der Platformer-Filter wirklich Platformer-Level?
        if (isPlatCat() && !platLenFallback && page == 0) {
            bool anyPlat = false;
            for (auto& e : list) anyPlat = anyPlat || e.platformer;
            if (!anyPlat) {
                log::warn("Platformer-Filter greift nicht -> Platformer werden aus allen Leveln herausgesucht");
                platLenFallback = true;
                total = -1; list.clear();
                ph = Ph::Page;
                return;
            }
        }

        // Selbsttest: greift der Schwierigkeitsfilter? Dann haben (fast) alle Level dieselbe Demon-Stufe.
        if (isDemonCat() && !demonAll && page == 0 && list.size() >= 3) {
            std::map<int, int> count;
            for (auto& e : list) count[e.raw]++;
            int best = 0;
            for (auto& [k, v] : count) best = std::max(best, v);
            if (best < static_cast<int>(list.size()) - 1) {
                log::warn("Demon-Filter greift nicht ({} von {} gleich) -> alle Demons gemischt", best, list.size());
                demonAll = true;
                cat = firstDemonCat();
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
        if (page == 0 && isPlatCat() && !platLenFallback) {
            // Platformer-Filter wird vom Server evtl. nicht angenommen
            platLenFallback = true; total = -1;
            ph = Ph::Page;
            return;
        }
        if (page == 0 && isDemonCat() && !demonAll) {
            // Demon-Filterwert wird vom Server evtl. nicht angenommen
            demonAll = true; cat = firstDemonCat(); total = -1;
            ph = Ph::Page;
            return;
        }
        if (page == 0) { stop("Der Server liefert keine Level (evtl. IP-Sperre). Spaeter weitermachen."); return; }
        // Mitten in der Liste: Rest dieser Stufe auslassen, die Nachlese holt Verpasstes
        endOfFilter();
    }

    void endOfFilter() {
        page = 0; total = -1; list.clear(); idx = 0;
        // Im Notfall-Modus "Demons gemischt" sind alle Demon-Stufen eines Typs schon erledigt
        int next = (isDemonCat() && demonAll) ? nextInMask(mask, (catType(cat) + 1) * CAT_COUNT - 1)
                                              : nextInMask(mask, cat);
        if (next >= 0) {
            cat = next;
            ph = Ph::Page;
        } else if (pass == 1) {
            pass = 2; cat = firstCat();
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
            if (e.platformer != isPlatCat() || failedIds.count(e.id)) { idx++; continue; }
            if (isDemonCat() && demonAll && !allDemonsSelected()) {
                // Notfall-Modus: Demon-Stufen, die nicht ausgewaehlt sind, auslassen
                int c = rawToCat(e.raw);
                if (c >= 0 && !diffInMask(mask, c)) { idx++; continue; }
            }
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
        pbotStarted = false;
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
        if (pl->m_isPlatformer) {
            watchPlatformer(pl, t);
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

    // Platformer: Noclip allein reicht nicht, der Platformer-Bot sucht erst einen Weg
    void watchPlatformer(PlayLayer* pl, double t) {
        auto& pb = PBot::get();
        if (!pbotStarted) {
            if (t - tPhase > 1.0 && !typeinfo_cast<CCTransitionScene*>(CCDirector::get()->getRunningScene())) {
                pbotStarted = true;
                pb.begin(pl);
            }
            return;
        }
        if (!pb.failReason.empty()) {
            auto why = pb.failReason;
            pb.failReason.clear();
            leaveLevel(pl, false, why);
            return;
        }
        if (pb.active()) return;   // sucht oder spielt gerade
        // Weg wurde gespielt, Spiel macht die Abschluss-Animation: kurz warten
        double limit = PBot::maxSearchSec() + maxReal();
        if (t - tLevelStart > limit) leaveLevel(pl, false, "Zeitlimit (Platformer)");
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
        if (level->isPlatformer() != isPlatCat()) { idx++; ph = Ph::Next; return; }
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
        if (PBot::get().active()) return;   // der Platformer-Bot laedt staendig Zustaende
        if (controls(pl) && pl->m_gameState.m_levelTime > 1.0) resets++;
    }
};

// Schwierigkeits-Symbol aus GD (falls vorhanden)
CCSprite* diffIcon(int c) {
    static const char* frames[CAT_COUNT] = {
        "diffIcon_auto_btn_001.png", "diffIcon_01_btn_001.png", "diffIcon_02_btn_001.png",
        "diffIcon_03_btn_001.png", "diffIcon_04_btn_001.png", "diffIcon_05_btn_001.png",
        "diffIcon_07_btn_001.png", "diffIcon_08_btn_001.png", "diffIcon_06_btn_001.png",
        "diffIcon_09_btn_001.png", "diffIcon_10_btn_001.png",
    };
    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frames[c])) return nullptr;
    return CCSprite::createWithSpriteFrameName(frames[c]);
}

// Auswahl, welche Schwierigkeiten gespielt werden; danach neu starten
class ChoosePopup : public Popup {
protected:
    int m_mask = MASK_DEMONS | TYPE_CLASSIC;
    CCMenuItemToggler* m_togglers[CAT_COUNT] = {};

    void addLabel(const char* text, float x, float y, float scale) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(scale);
        lbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(lbl, Anchor::BottomLeft, {x, y});
    }

    bool init() {
        constexpr float W = 400.f, H = 305.f;
        if (!Popup::init(W, H)) return false;
        this->setTitle("Was soll gespielt werden?");

        m_mask = Mod::get()->getSavedValue<int>("selection-mask", -1);
        if (m_mask < 0) m_mask = legacyMask();
        m_mask = fixMask(m_mask);

        // Oben: Level-Typ
        const int typeBits[2] = {TYPE_CLASSIC, TYPE_PLAT};
        const char* typeNames[2] = {"Classic", "Platformer"};
        for (int t = 0; t < 2; t++) {
            float x = t == 0 ? 70.f : 225.f;
            float y = H - 50.f;
            int bit = typeBits[t];
            auto tog = CCMenuItemExt::createTogglerWithStandardSprites(0.65f, [this, bit](CCMenuItemToggler*) {
                m_mask ^= bit;
            });
            tog->toggle((m_mask & bit) != 0);
            m_buttonMenu->addChildAtPosition(tog, Anchor::BottomLeft, {x, y});
            addLabel(typeNames[t], x + 18.f, y, 0.5f);
        }

        // Links: normale Schwierigkeiten, rechts: Demons
        for (int i = 0; i < CAT_COUNT; i++) {
            bool demon = i >= FIRST_DEMON;
            int row = demon ? i - FIRST_DEMON : i;
            float x = demon ? 195.f : 28.f;
            float y = H - 82.f - row * 24.f;

            auto tog = CCMenuItemExt::createTogglerWithStandardSprites(0.55f, [this, i](CCMenuItemToggler*) {
                m_mask ^= (1 << i);
            });
            tog->toggle(diffInMask(m_mask, i));
            m_buttonMenu->addChildAtPosition(tog, Anchor::BottomLeft, {x, y});
            m_togglers[i] = tog;

            float lx = x + 17.f;
            if (auto icon = diffIcon(i)) {
                icon->setScale(0.38f);
                m_mainLayer->addChildAtPosition(icon, Anchor::BottomLeft, {lx + 9.f, y});
                lx += 22.f;
            }
            addLabel(CATS[i].single, lx, y, 0.36f);
        }

        // Schnellauswahl (nur Schwierigkeiten)
        auto quick = [this](const char* text, float x, int mask) {
            auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.55f);
            auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, mask](CCMenuItemSpriteExtra*) { this->setDiffs(mask); });
            m_buttonMenu->addChildAtPosition(btn, Anchor::Bottom, {x, 60.f});
        };
        quick("Alle", -110.f, MASK_ALL);
        quick("Nur Demons", 0.f, MASK_DEMONS);
        quick("Keine", 110.f, 0);

        auto startSpr = ButtonSprite::create("Start", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        auto startBtn = CCMenuItemExt::createSpriteExtra(startSpr, [this](CCMenuItemSpriteExtra*) { this->onStart(); });
        m_buttonMenu->addChildAtPosition(startBtn, Anchor::Bottom, {0.f, 25.f});
        return true;
    }

    void setDiffs(int diffs) {
        m_mask = (m_mask & ~DIFF_BITS) | (diffs & DIFF_BITS);
        for (int i = 0; i < CAT_COUNT; i++) {
            if (m_togglers[i]) m_togglers[i]->toggle(diffInMask(m_mask, i));
        }
    }

    void onStart() {
        if ((m_mask & DIFF_BITS) == 0) {
            Notification::create("Waehle mindestens eine Schwierigkeit aus", NotificationIcon::Warning)->show();
            return;
        }
        if ((m_mask & (TYPE_CLASSIC | TYPE_PLAT)) == 0) {
            Notification::create("Waehle Classic und/oder Platformer aus", NotificationIcon::Warning)->show();
            return;
        }
        int mask = m_mask;
        Mod::get()->setSavedValue<int>("selection-mask", mask);
        DemonBot::get()->start(false, mask);
        this->onClose(nullptr);
    }

public:
    static ChoosePopup* create() {
        auto ret = new ChoosePopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

void openChooser() {
    if (auto p = ChoosePopup::create()) p->show();
}

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
            createQuickPopup("Bot",
                fmt::format("{}\n\nGespeicherter Stand: {}", info, bot->savedPosition()),
                "Neu starten", "Weiter",
                [](FLAlertLayer*, bool weiter) {
                    if (weiter) DemonBot::get()->start(true);
                    else openChooser();
                },
                true, true);
        } else {
            createQuickPopup("Bot", info, "Abbrechen", "Start",
                [](FLAlertLayer*, bool go) { if (go) openChooser(); },
                true, true);
        }
    }
};

// ---------- Level: Anzeige fuers Video, Ende erkennen, Auto-Pause verhindern ----------
class $modify(BotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        PBot::get().shutdown(false);   // neues Level: alten Platformer-Zustand vergessen
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
        auto& pb = PBot::get();
        if (pb.active() && pb.pl == this) {
            if (pb.searching()) {
                pb.reached = true;   // Ziel bei der Suche gefunden: noch NICHT abschliessen
                return;
            }
            pb.handover();           // sauberer Lauf: ab hier normal abschliessen
        }
        PlayLayer::levelComplete();
        DemonBot::get()->onLevelComplete(this);
    }

    void playPlatformerEndAnimationToPos(CCPoint pos, bool instant) {
        auto& pb = PBot::get();
        if (pb.active() && pb.pl == this) {
            if (pb.searching()) {
                pb.reached = true;
                return;
            }
            pb.handover();
        }
        PlayLayer::playPlatformerEndAnimationToPos(pos, instant);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& pb = PBot::get();
        if (pb.active() && pb.pl == this && object != m_anticheatSpike) {
            pb.dead = true;   // merken, aber nicht wirklich sterben
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void checkpointActivated(CheckpointGameObject* object) {
        auto& pb = PBot::get();
        pb.inCheckpointActivated = true;
        PlayLayer::checkpointActivated(object);
        pb.inCheckpointActivated = false;
    }

    // Keine automatischen Practice-Checkpoints, solange der Platformer-Bot laeuft
    CheckpointObject* markCheckpoint() {
        auto& pb = PBot::get();
        if (pb.active() && pb.pl == this && !pb.inCheckpointActivated) return nullptr;
        return PlayLayer::markCheckpoint();
    }

    void updateVisibility(float dt) {
        PlayLayer::updateVisibility(dt);
        auto& pb = PBot::get();
        if (pb.pl == this) {
            for (auto& obj : pb.dummies) obj->setOpacity(0);
        }
    }

    void resetLevelFromStart() {
        auto& pb = PBot::get();
        if (pb.active() && pb.pl == this && !pb.inRestore) pb.shutdown(true);
        PlayLayer::resetLevelFromStart();
    }

    void onQuit() {
        if (PBot::get().pl == this) PBot::get().shutdown(false);
        PlayLayer::onQuit();
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

// ---------- Platformer-Bot steuert die Zeit und die Eingaben selbst ----------
class $modify(BotGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto& pb = PBot::get();
        if (pb.active() && !pb.inStep && pb.pl && static_cast<GJBaseGameLayer*>(pb.pl) == this) {
            pb.frame(dt);
            return;
        }
        GJBaseGameLayer::update(dt);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& pb = PBot::get();
        if (pb.active() && !pb.botInput && pb.pl && static_cast<GJBaseGameLayer*>(pb.pl) == this) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

// ---------- Waehrend der Platformer-Suche nichts zaehlen (Spruenge, Coins usw.) ----------
class $modify(BotStats, GameStatsManager) {
    void incrementStat(char const* key, int amount) {
        if (PBot::get().searching()) return;
        GameStatsManager::incrementStat(key, amount);
    }
    void storeUserCoin(char const* key) {
        if (PBot::get().searching()) return;
        GameStatsManager::storeUserCoin(key);
    }
    void storeSecretCoin(char const* key) {
        if (PBot::get().searching()) return;
        GameStatsManager::storeSecretCoin(key);
    }
    void completedLevel(GJGameLevel* level) {
        if (PBot::get().searching()) return;
        GameStatsManager::completedLevel(level);
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
