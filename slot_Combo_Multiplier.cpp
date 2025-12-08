/*
遊戲規則：
Cascading Way Game : 1024 ways  window : 4*5
遊戲盤面由左到右，同符號連續出現3、4、5次，即有贏分，符號消除後會補上新符；直到無贏分
贏分計算為：押注 x 符號賠率 x 符號在各軸個數
主遊戲依每轉消除次數，當次消除贏分倍率依序x1 x2 x3 x4，後續皆x4
免費遊戲依消除次數跨轉累計，當次消除贏分倍率依序x1 x2 x4 x6…至x50封頂
主遊戲中出現3個任意位置的Scatter觸發免費遊戲x8，每多1個再+2
免費遊戲中出現3個任意位置的Scatter再觸發免費遊戲x8，每多1個再+2
免費遊戲上限為50轉，Scatter出現在主遊戲1-5轉輪，免費遊戲2-4轉輪
Wild皆出現在2-5轉輪，可替代除Scatter外之任意符號
賠率表以1024 ways總押注1作計算

程式流程：
依處理器thread數 → 分數個worker → 各自做以下 1–7 → 彙整輸出

1. 主遊戲初轉（MG）

    呼叫 w.spinInit(rng, reelsMGSpin)
    對 5 軸各抽一個停點，填滿 4×5 視窗，清空 Hybrid 補帶狀態。

2. 主遊戲 ways 計算 + 連消（含 AftX_MG）

    進入 MG 迴圈（playMGSpin 裡）：
        a. 呼叫 evalWays(w) 算本步 ways 派彩，無贏分就跳出 MG。
        b. 第一次有贏分的盤面：bumpLenCats 記到 mgInitLenCount。
        c. 每一步（初轉＋每次消除後）都用 bumpLenCats 記到 mgLenCount。
        d. 將本步贏分乘上目前 MG 倍率（1→2→3→4 封頂）加到 mgWin，combo++。
        e. 若尚未切 AftX，且 combo ≥ AftComboX_MG，
           則以機率 pAftX_MG 把補帶換成 reelsMGRefillAftX，並 resetHybridRefillState。
        f. 依目前選到的補帶（普通 / AftX）呼叫 applyCascadesHybrid
           做「消除 + 掉落 + 補符」，MG 倍率 +1（最多 4）。
    MG 迴圈結束後，記錄這把 MG combo 次數進 mgComboHist。

3. 判斷是否觸發 FG

    呼叫 countScatterAll(w) 數整個 4×5 視窗的 Scatter（MG 結束後盤面）。
        s < 3：沒有 FG。若 MG 也沒贏分，deadSpins++。
        s ≥ 3：觸發 FG：
            a. 由 startSpinsByScatter(s) 算出初始場次 fgStart（8 起跳，上限 50）。
            b. triggerCount++，fgStartDist[fgStart]++。

4. 一整段 FG（含 AftX_FG + END 機制）

    在 workerFunc 中呼叫 playFG(...)，
    傳入：FG 轉輪、補帶、起始場次 fgStart，以及各種 FG 統計陣列。

    playFG 內部流程（這一整段 FG）：
        a. queue = initSpins（目前排隊要跑的 FG 轉數），mult = 1（跨轉累計倍率 1,2,4,6…50），segPeak = 1。
        b. 當 queue > 0 時，依序跑每一轉（queue--，spins++，turnIndex = 這段的第幾轉）：

            初始輪帶：
                若 mult ≥ aftMultX_FG_EndSpin，以 pEndSpin 機率改用 reelsFG_END_Spin，並計數 usedEndSpin；
                否則用普通 FG spin 帶 reelsFGSpin。
                → 呼叫 w.spinInit(選到的 spin 帶)。

            這一轉內再進入「連消」迴圈：
                呼叫 evalWays(w) 算贏分，沒贏分就結束這一轉。
                第一次有贏分：記到 fgInitLenCount；每一步都記到 fgLenCount。
                把本步贏分乘上目前跨轉倍率 mult，加到 res.total；turnCombo++。

                判斷該步補帶：
                    若 mult ≥ aftMultX_FG_EndRefill：
                        優先以 pEndRefill 切入 END_Refill（reelsFG_END_Refill，整轉補符鎖在 END 模式），並記錄第一次切入的 turnIndex；
                    若沒有切 END 且目前仍是 Normal：
                        以 pAftX_FG_AftMultX 切到 AftX 補帶 reelsFGRefillAftX。
                    若 mult 還沒到門檻：
                        沿用原先 combo 版 AftX 邏輯（turnCombo ≥ AftComboX_FG 時，以 pAftX_FG 切到 AftX）。
                    每次模式切換都 resetHybridRefillState。
                    用目前選到的補帶（Normal / AftX / END）呼叫 applyCascadesHybrid，做消除 + 補符。

                FG 跨轉倍率更新：
                    呼叫 nextFGMult(mult)，依序 1→2→4→6→…→50 封頂，同時更新這段的 segPeak。

            這轉連消結束後，數盤面 Scatter：
                s ≥ 3 則算要再加幾轉 add = retriggerByScatter(s)，
                再依「本段 FG 已跑 + 排隊中」總數配合 maxFGTotalSpins=50 做截斷；
                add > 0 則 queue += add，retri++，
                並把 add 依 8,10,12.. 併入 retriggerDist。
                把這轉 turnCombo 併到 fgComboHist（>20 併入 20），更新整段最長 FG combo。

5. 整段 FG 派彩加權、累計

    以 segPeak 更新 peakMultHist、peakMultAvg、peakMultMax。
    把該段實際總場次 spins 併到 fgSegLenHist（1..50）。
    END 使用次數與第一次 END_Refill 切入轉數透過 FGRunResult 回傳給 worker。
    worker 把本段 FG 的贏分（res.total）、spins、retri、END 統計等，累加到自己的 local Stats。

6. 單把結果累計與分層

    該把總贏分 spinTotal = mgWin + fgWin。
    依 spinTotal / bet，更新 Big / Mega / Super / Holy / Jumbo / Jojo 分層，並更新 maxSingleSpin。

7. 進度心跳

    在 worker 中，每轉 bumpCnt++，累積到 4096 轉時：
        spinsDone.fetch_add(4096)；e 印出目前進度、速度與預估剩餘時間。
    progressThread 依 spinsDon
*/

#ifdef _WIN32
#include <windows.h> // 把 Windows 主控台碼頁切到 UTF-8（避免 中文/符號 亂碼）
#endif

#include <iostream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

using namespace std;

/**************
 * 參數
 **************/
static int64_t numSpins = 100000000;
static double bet = 1.0;
static unsigned workers = thread::hardware_concurrency() ? thread::hardware_concurrency() : 4;

// AftX
static int AftComboX_MG = 3;
static double pAftX_MG = 0.35;

static int AftComboX_FG = 2;
static double pAftX_FG = 0.4;

// FG END 機制
// END_Refill 門檻與機率
static int aftMultX_FG_EndRefill = 8;
static double pEndRefill = 0.5;
static double pAftX_FG_AftMultX = 0.5;

// END_Spin 門檻與機率
static int aftMultX_FG_EndSpin = 10;
static double pEndSpin = 0.45;

/**************
 * 常數
 **************/
const int reelsCount = 5;
const int rows = 4;
const int numWays = 1024;
const int minPayLen = 3;
const int maxLen = 5;

const int maxFGTotalSpins = 50;

/**************
 * 符號
 **************/
enum Symbol : uint8_t
{
    S9,
    S10,
    SJ,
    SQ,
    SK,
    SB,
    SF,
    SR,
    SW, // Wild
    SS, // Scatter
    NumSymbols
};

/**************
 * 賠率表（3/4/5 連）
 **************/
array<array<double, 3>, NumSymbols> pay = []
{
    array<array<double, 3>, NumSymbols> p{};
    p[S9] = {0.04, 0.10, 0.20};
    p[S10] = {0.04, 0.15, 0.30};
    p[SJ] = {0.06, 0.20, 0.40};
    p[SQ] = {0.06, 0.25, 0.50};
    p[SK] = {0.10, 0.40, 1.00};
    p[SB] = {0.15, 0.60, 1.20};
    p[SF] = {0.20, 1.00, 1.80};
    p[SR] = {0.30, 1.20, 2.50};
    return p;
}();

/**************
 * 輪帶字串
 **************/
vector<vector<string>> reelsMGSpinStr = {
    // Reel 1
    {"9", "9", "Q", "Q", "K", "K", "K", "R", "R", "S", "9", "9", "J", "J", "K", "K", "F", "F", "J", "J", "9", "9", "9", "S", "F", "F", "J", "J", "9", "9", "Q", "Q", "J", "J", "R", "R", "10", "10", "F", "F", "J", "J", "K", "K", "9", "9", "R", "R", "Q", "Q", "B", "B", "K", "K", "B", "B", "9", "9", "K", "K", "9", "9", "J", "J", "J", "F", "F", "K", "K", "Q", "Q", "F", "F", "10", "10", "K", "K", "J", "J", "F", "F", "Q", "Q", "F", "F", "J", "J", "J", "9", "9", "K", "K", "R", "S", "Q", "Q", "F", "F", "10", "10", "F", "F", "9", "9", "K", "K", "S", "9", "9", "10", "10", "K", "K", "K", "10", "10", "9", "9", "Q", "Q", "9", "9", "K", "K", "Q", "Q", "J", "J", "9", "9", "9", "K", "K", "S", "10", "10", "J", "J", "10", "10", "S", "J", "J", "B", "B", "F", "F", "J", "J", "S", "K", "K", "10", "10", "J", "J", "9", "9", "B", "B", "F", "F"},
    // Reel 2
    {"R", "R", "Q", "Q", "B", "B", "S", "Q", "Q", "R", "R", "J", "J", "B", "B", "K", "K", "W", "10", "10", "J", "J", "B", "B", "9", "9", "R", "R", "F", "F", "J", "J", "10", "10", "S", "J", "J", "Q", "Q", "Q", "K", "K", "10", "10", "R", "R", "K", "K", "Q", "Q", "10", "10", "Q", "Q", "10", "10", "J", "J", "B", "B", "K", "K", "K", "R", "R", "J", "J", "9", "9", "F", "F", "10", "10", "B", "B", "10", "10", "K", "K", "S", "R", "R", "B", "B", "W", "9", "9", "F", "F", "S", "Q", "Q", "B", "B", "10", "10", "Q", "Q", "10", "10", "R", "R", "B", "B", "10", "10", "S", "9", "9", "R", "R", "B", "B", "9", "9", "S", "R", "R", "Q", "Q", "J", "J", "9", "9", "S", "R", "R", "B", "B", "J", "J", "Q", "Q", "10", "10", "Q", "Q", "B", "B", "Q", "Q", "10", "10", "B", "B", "10", "10", "Q", "Q", "Q", "B", "B", "Q", "Q", "10", "10", "B", "B", "R", "R", "9", "9"},
    // Reel 3
    {"Q", "Q", "J", "J", "S", "9", "9", "K", "K", "10", "10", "B", "B", "S", "9", "9", "R", "R", "10", "10", "9", "9", "K", "K", "S", "9", "9", "Q", "Q", "J", "J", "10", "10", "F", "F", "9", "9", "J", "J", "10", "10", "B", "B", "B", "9", "9", "10", "10", "J", "J", "K", "K", "F", "F", "R", "R", "10", "10", "J", "J", "F", "F", "Q", "Q", "10", "10", "B", "B", "9", "9", "Q", "Q", "K", "K", "10", "10", "B", "B", "F", "F", "B", "B", "R", "R", "Q", "Q", "R", "R", "K", "K", "Q", "Q", "9", "9", "10", "10", "K", "K", "9", "9", "K", "K", "Q", "Q", "9", "9", "10", "10", "F", "F", "J", "J", "W", "10", "10", "Q", "Q", "10", "10", "J", "J", "B", "B", "K", "K", "10", "10", "Q", "Q", "Q", "B", "B", "9", "9", "10", "10", "J", "J", "F", "F", "9", "9", "K", "K", "W", "9", "9", "Q", "Q", "J", "J", "J", "S", "9", "9", "B", "B", "J", "J", "S", "9", "9"},
    // Reel 4
    {"9", "10", "Q", "K", "S", "J", "B", "R", "F", "9", "J", "W", "10", "B", "Q", "F", "J", "R", "W", "9", "10", "Q", "R", "9", "J", "B", "K", "10", "F", "R", "9", "K", "J", "F", "9", "Q", "B", "F", "10", "J", "9", "B", "J", "10", "Q", "9", "J", "B", "F", "K", "9", "B", "10", "Q", "K", "R", "F", "J", "Q", "9", "B", "10", "Q", "B", "J", "10", "9", "K", "B", "Q", "J", "K", "F", "9", "J", "F", "10", "9", "J", "R", "Q", "B", "F", "10", "9", "J", "B", "K", "Q", "R", "F", "J", "10", "9", "J", "F", "10", "R", "Q", "B", "10", "R", "F", "B", "10", "F", "B", "9", "K", "F", "9", "B", "10", "9", "R", "K", "10", "J", "R", "9", "Q", "9", "B", "R", "K", "9", "J", "B", "Q", "10", "9", "K", "J", "R", "10", "K", "S", "Q", "R", "B", "K", "J", "Q", "9", "10", "J", "B", "R", "K", "Q", "F", "10", "B", "9", "Q", "J", "10", "K", "Q", "R", "J", "B"},
    // Reel 5
    {"Q", "F", "S", "K", "B", "R", "10", "J", "B", "R", "W", "9", "Q", "R", "J", "B", "Q", "K", "10", "F", "J", "B", "10", "F", "Q", "9", "10", "J", "R", "9", "B", "F", "10", "B", "Q", "J", "R", "10", "B", "J", "Q", "10", "9", "R", "Q", "10", "9", "J", "F", "B", "R", "Q", "9", "J", "S", "10", "J", "Q", "B", "9", "10", "F", "J", "9", "10", "Q", "J", "9", "B", "Q", "10", "J", "R", "Q", "10", "9", "J", "F", "B", "Q", "K", "10", "J", "Q", "9", "10", "B", "J", "F", "9", "B", "Q", "10", "R", "9", "B", "J", "Q", "10", "R", "F", "J", "9", "B", "K", "10", "Q", "9", "J", "10", "F", "9", "Q", "K", "J", "F", "Q", "W", "10", "9", "K", "F", "K", "10", "K", "9", "Q", "R", "K", "J", "10", "R", "Q", "9", "K", "F", "B", "Q", "K", "9", "J", "F", "K", "J", "9", "R", "K", "10", "J", "9", "K", "Q", "J", "9", "10", "B", "Q", "9", "10", "J", "K", "9"},
};

vector<vector<string>> reelsFGSpinStr = {
    // Reel 1
    {"10", "9", "Q", "F", "K", "B", "9", "J", "R", "F", "K", "Q", "R", "9", "10", "J", "9", "B", "J", "Q", "10", "K", "J", "9", "Q", "F", "J", "10", "R", "B", "9", "10", "Q", "F", "J", "10", "9", "F", "R", "B", "9", "J", "Q", "9", "10", "J", "K", "Q", "R", "J", "9", "10", "R", "F", "J", "B", "10", "F", "R", "B", "9", "10", "K", "R", "Q", "B", "10", "9", "J", "K", "10", "Q", "9", "J", "B", "10", "9", "J", "B", "K", "Q", "F", "J", "K", "9", "Q", "J", "K", "10", "Q", "J", "R", "K", "9", "Q", "10", "F", "J", "B", "Q", "9", "J", "10", "R", "F", "10", "9", "R", "B", "Q", "F", "9", "J", "10", "K", "B", "R", "9", "10", "Q", "K", "J", "Q", "10", "9", "F", "B", "K", "Q", "9", "B", "K", "Q", "F", "10", "K"},
    // Reel 2
    {"10", "Q", "9", "K", "J", "10", "F", "R", "Q", "B", "F", "9", "Q", "K", "R", "B", "W", "10", "9", "F", "10", "Q", "9", "J", "R", "10", "F", "9", "Q", "10", "B", "S", "K", "9", "10", "B", "Q", "9", "10", "J", "R", "F", "K", "9", "J", "R", "10", "B", "Q", "K", "9", "B", "J", "K", "10", "B", "R", "Q", "9", "10", "K", "Q", "J", "10", "9", "Q", "J", "K", "B", "9", "J", "K", "Q", "10", "S", "R", "J", "9", "10", "Q", "K", "J", "R", "9", "Q", "10", "K", "J", "F", "Q", "10", "J", "K", "9", "F", "S", "Q", "J", "F", "9", "Q", "10", "F", "B", "B", "9", "9", "B", "B", "Q", "Q", "10", "10", "J", "J", "S", "F", "F", "9", "9", "R", "R", "S", "J", "J", "10", "10", "K", "K", "Q", "Q", "F", "F", "J", "J", "K", "K", "R", "R", "B", "B", "S"},
    // Reel 3
    {"Q", "Q", "9", "9", "10", "10", "K", "K", "J", "J", "B", "B", "F", "F", "R", "R", "W", "Q", "Q", "9", "9", "J", "J", "Q", "Q", "F", "F", "9", "9", "Q", "Q", "S", "J", "J", "9", "9", "R", "R", "Q", "Q", "B", "B", "10", "10", "9", "9", "9", "J", "J", "J", "10", "10", "Q", "Q", "Q", "R", "R", "J", "J", "K", "K", "B", "B", "9", "9", "9", "K", "K", "K", "Q", "Q", "10", "10", "10", "S", "J", "J", "J", "B", "B", "K", "K", "9", "9", "Q", "Q", "J", "J", "10", "10", "10", "F", "F", "9", "9", "S", "K", "K", "K", "F", "F", "10", "10", "B", "B", "9", "9", "R", "R", "K", "K", "Q", "Q", "10", "10", "S", "F", "F", "9", "9", "B", "B", "S", "10", "10", "Q", "Q", "J", "J", "K", "K", "F", "F", "10", "10", "J", "J", "B", "B", "R", "R", "S"},
    // Reel 4
    {"9", "K", "Q", "R", "K", "B", "9", "J", "R", "F", "K", "Q", "R", "9", "10", "J", "W", "K", "J", "9", "F", "K", "J", "9", "Q", "R", "J", "9", "R", "K", "J", "S", "R", "K", "F", "J", "10", "9", "F", "R", "B", "F", "J", "K", "9", "R", "B", "K", "Q", "R", "J", "9", "Q", "R", "F", "J", "B", "K", "F", "R", "B", "9", "10", "K", "R", "J", "B", "10", "9", "R", "K", "10", "J", "9", "S", "J", "K", "10", "9", "J", "B", "K", "Q", "R", "J", "K", "R", "9", "B", "K", "9", "Q", "J", "R", "K", "S", "J", "Q", "10", "R", "J", "B", "Q", "9", "J", "9", "R", "F", "10", "9", "R", "K", "J", "F", "9", "S", "J", "9", "K", "J", "R", "9", "S", "10", "J", "K", "R", "Q", "10", "9", "R", "B", "K", "J", "9", "B", "K", "Q", "F", "9", "K", "S"},
    // Reel 5
    {"10", "Q", "B", "Q", "R", "B", "10", "Q", "F", "R", "10", "9", "K", "F", "10", "Q", "W", "B", "F", "R", "10", "F", "K", "Q", "B", "10", "R", "F", "Q", "B", "F", "Q", "10", "B", "F", "Q", "B", "10", "9", "B", "F", "B", "R", "F", "Q", "B", "K", "R", "10", "B", "F", "K", "B", "Q", "J", "F", "B", "R", "Q", "9", "B", "10", "R", "J", "10", "F", "B", "10", "Q", "10", "R", "Q", "10", "F", "B", "10", "F", "Q", "K", "10", "B", "9", "F", "Q", "B", "J", "K", "B", "10", "Q", "9", "K", "Q", "F", "9", "Q", "K", "9", "J", "10", "Q", "K", "9", "10", "J", "F", "10", "9", "K", "Q", "J", "9", "10", "F", "B", "F", "J", "K", "R", "J", "9", "10", "Q", "F", "J", "B", "10", "Q", "F", "J", "10", "F", "10", "Q", "B", "J"},
};

vector<vector<string>> reelsMGRefillStr = {
    // Reel 1
    {"Q", "10", "J", "9", "9", "K", "Q", "B", "F", "R", "S", "9", "B", "J", "R", "K", "K", "9", "10", "F", "Q", "9", "10", "10", "F", "Q", "9", "B", "B", "J", "J", "R", "K", "J", "9", "B", "B", "R", "10", "K", "10", "F", "10", "Q", "J", "R", "F", "9", "Q", "K", "10", "F", "9", "B", "R", "F", "J", "10", "R", "Q", "K", "9", "B", "10", "9", "Q", "B", "10", "K", "J", "R", "9", "J", "K", "10", "Q", "B", "9", "Q", "J", "J", "K", "10", "F", "9", "Q", "10", "K", "J", "B", "9", "Q", "9", "J", "K", "J", "10", "K", "9", "10", "Q", "Q", "J", "S", "9", "10", "J", "F", "Q", "9", "J", "J", "Q", "10", "9", "10", "Q"},
    // Reel 2
    {"R", "B", "10", "9", "9", "10", "W", "F", "9", "Q", "S", "10", "F", "Q", "9", "B", "10", "W", "J", "R", "K", "W", "J", "J", "R", "K", "10", "W", "J", "Q", "Q", "9", "B", "Q", "10", "F", "B", "9", "W", "B", "J", "Q", "B", "K", "Q", "9", "R", "W", "K", "B", "J", "F", "10", "F", "9", "R", "Q", "J", "R", "K", "B", "10", "J", "W", "10", "K", "10", "J", "9", "Q", "R", "10", "Q", "9", "J", "K", "B", "10", "K", "Q", "W", "9", "J", "F", "10", "K", "J", "9", "Q", "B", "10", "K", "W", "Q", "9", "F", "J", "9", "10", "J", "J", "K", "Q", "W", "10", "J", "Q", "F", "9", "10", "9", "K", "9", "J", "10", "9", "Q"},
    // Reel 3
    {"J", "F", "10", "R", "R", "K", "9", "Q", "9", "B", "S", "J", "Q", "K", "10", "F", "F", "10", "Q", "9", "B", "W", "Q", "Q", "9", "B", "J", "B", "R", "K", "K", "10", "F", "K", "J", "Q", "B", "10", "Q", "J", "Q", "9", "B", "B", "K", "10", "9", "F", "B", "J", "Q", "F", "J", "R", "10", "9", "K", "Q", "R", "10", "F", "J", "R", "W", "J", "9", "B", "Q", "10", "K", "R", "9", "Q", "10", "K", "J", "10", "J", "9", "K", "9", "10", "Q", "F", "J", "9", "Q", "10", "K", "B", "J", "9", "10", "9", "10", "F", "Q", "10", "9", "K", "J", "J", "Q", "W", "J", "Q", "9", "F", "10", "J", "10", "9", "Q", "9", "10", "9", "R"},
    // Reel 4
    {"9", "J", "K", "R", "9", "K", "J", "K", "9", "R", "S", "F", "J", "K", "9", "J", "R", "9", "F", "K", "J", "W", "K", "9", "B", "R", "F", "K", "B", "Q", "K", "J", "R", "B", "J", "9", "B", "J", "R", "9", "K", "10", "B", "F", "Q", "9", "K", "R", "Q", "R", "K", "F", "9", "J", "K", "F", "J", "9", "W", "9", "B", "K", "R", "J", "B", "K", "9", "F", "R", "K", "K", "J", "J", "R", "K", "9", "J", "9", "J", "10", "K", "R", "J", "10", "R", "J", "K", "9", "J", "K", "9", "R", "J", "Q", "R", "K", "Q", "10", "R", "J", "R", "10", "Q", "Q", "9", "K", "9", "W", "J", "Q", "9", "R", "10", "K", "J", "R", "10"},
    // Reel 5
    {"10", "Q", "B", "F", "J", "Q", "B", "F", "10", "R", "S", "B", "F", "Q", "F", "K", "10", "9", "Q", "B", "R", "W", "B", "10", "K", "F", "Q", "B", "R", "F", "10", "Q", "9", "F", "B", "10", "B", "Q", "10", "9", "B", "J", "R", "Q", "B", "F", "J", "F", "R", "10", "B", "F", "K", "10", "Q", "Q", "F", "B", "10", "9", "R", "Q", "B", "W", "10", "K", "F", "10", "Q", "F", "10", "B", "Q", "10", "R", "K", "B", "Q", "10", "B", "J", "K", "Q", "F", "10", "Q", "10", "J", "K", "B", "10", "Q", "9", "B", "Q", "F", "10", "B", "Q", "J", "10", "9", "F", "W", "9", "10", "J", "F", "Q", "10", "9", "B", "Q", "Q", "9", "10", "F"},
};

vector<vector<string>> reelsFGRefillStr = {
    // Reel 1
    {"Q", "10", "J", "9", "9", "K", "Q", "B", "F", "R", "9", "B", "J", "R", "K", "K", "9", "10", "F", "Q", "9", "10", "10", "F", "Q", "9", "B", "B", "J", "J", "R", "K", "J", "9", "B", "B", "R", "10", "K", "10", "F", "B", "Q", "J", "R", "F", "K", "Q", "K", "10", "F", "9", "B", "R", "F", "J", "10", "R", "Q", "K", "9", "B", "10", "9", "Q", "B", "10", "K", "J", "R", "9", "J", "K", "10", "Q", "B", "9", "Q", "J", "J", "K", "10", "F", "9", "Q", "10", "K", "J", "B", "9", "Q", "9", "J", "K", "F", "10", "K", "9", "10", "Q", "Q", "J", "9", "10", "J", "F", "Q", "9", "J", "J", "Q", "10", "9", "10", "R"},
    // Reel 2
    {"R", "B", "10", "9", "9", "J", "W", "F", "K", "Q", "S", "10", "F", "Q", "9", "B", "B", "W", "J", "R", "K", "W", "J", "J", "R", "K", "10", "W", "F", "Q", "Q", "9", "B", "Q", "10", "F", "B", "9", "W", "B", "J", "R", "B", "K", "Q", "9", "R", "W", "K", "B", "J", "F", "10", "F", "9", "R", "Q", "J", "R", "K", "B", "10", "F", "W", "10", "K", "B", "J", "9", "Q", "R", "10", "Q", "9", "J", "K", "B", "10", "K", "Q", "W", "9", "J", "F", "10", "K", "J", "9", "Q", "B", "10", "K", "W", "Q", "9", "F", "J", "9", "10", "J", "J", "K", "Q", "W", "10", "J", "Q", "F", "9", "10", "9", "K", "K", "J", "10", "9", "R"},
    // Reel 3
    {"J", "F", "10", "R", "R", "K", "9", "Q", "9", "B", "S", "J", "R", "K", "10", "F", "F", "10", "Q", "9", "B", "W", "Q", "Q", "9", "B", "J", "B", "R", "K", "K", "10", "F", "K", "J", "R", "B", "10", "Q", "F", "Q", "9", "B", "B", "K", "10", "9", "F", "B", "F", "Q", "F", "J", "R", "10", "9", "K", "Q", "R", "B", "F", "J", "R", "W", "J", "9", "B", "Q", "10", "K", "R", "9", "Q", "10", "K", "J", "B", "J", "9", "K", "9", "10", "Q", "F", "J", "9", "Q", "10", "K", "B", "J", "9", "10", "K", "10", "F", "Q", "10", "9", "K", "J", "J", "Q", "W", "J", "Q", "9", "F", "10", "J", "10", "K", "Q", "9", "10", "9", "R"},
    // Reel 4
    {"Q", "10", "R", "K", "K", "B", "J", "J", "F", "9", "S", "Q", "9", "B", "J", "R", "R", "J", "K", "10", "F", "W", "K", "K", "10", "F", "Q", "9", "9", "B", "B", "J", "R", "B", "Q", "9", "B", "J", "9", "R", "K", "10", "B", "F", "B", "J", "10", "F", "F", "R", "K", "F", "Q", "9", "J", "10", "B", "K", "R", "F", "R", "Q", "9", "W", "9", "J", "B", "K", "10", "Q", "R", "10", "K", "J", "9", "Q", "B", "9", "J", "Q", "10", "10", "K", "F", "9", "J", "K", "10", "Q", "B", "9", "J", "10", "Q", "10", "F", "K", "J", "10", "9", "Q", "Q", "K", "W", "Q", "9", "10", "F", "J", "9", "J", "K", "Q", "10", "9", "10", "R"},
    // Reel 5
    {"9", "K", "J", "Q", "Q", "R", "R", "B", "F", "10", "K", "10", "F", "Q", "9", "9", "J", "B", "J", "R", "W", "B", "B", "J", "R", "K", "10", "10", "F", "F", "Q", "9", "F", "K", "10", "B", "Q", "10", "9", "B", "J", "B", "R", "F", "Q", "J", "K", "R", "9", "B", "F", "K", "10", "Q", "J", "F", "B", "R", "R", "9", "K", "10", "W", "10", "Q", "B", "9", "J", "K", "R", "J", "9", "Q", "10", "K", "B", "10", "Q", "K", "J", "J", "9", "F", "10", "Q", "9", "J", "K", "B", "10", "Q", "9", "K", "J", "F", "9", "Q", "J", "10", "K", "K", "9", "W", "9", "10", "J", "F", "Q", "10", "9", "K", "Q", "J", "9", "10", "R"},
};

vector<vector<string>> reelsMGRefillStrAftX = {
    // Reel 1
    {"9", "9", "S", "10", "10", "J", "J", "Q", "Q", "K", "K", "B", "B", "F", "F", "R", "R", "9", "9", "J", "J", "J", "9", "9", "9", "Q", "Q", "10", "10", "J", "J", "K", "K", "10", "10", "B", "B", "9", "9", "9", "Q", "Q", "F", "F", "J", "J", "10", "10", "10", "R", "R", "F", "F", "K", "K", "B", "B", "R", "R", "9", "9", "Q", "Q", "10", "10", "10", "J", "J", "J", "10", "10", "9", "9", "9", "Q", "Q", "F", "F", "J", "J", "B", "B", "J", "J", "10", "10", "K", "K", "9", "9", "B", "B", "Q", "Q", "9", "9", "9", "K", "K", "10", "10", "K", "K", "Q", "Q"},
    // Reel 2
    {"Q", "Q", "J", "J", "S", "9", "9", "10", "10", "B", "B", "K", "K", "R", "R", "F", "F", "Q", "Q", "9", "9", "J", "J", "J", "Q", "Q", "B", "B", "B", "J", "J", "K", "K", "9", "9", "10", "10", "J", "J", "B", "B", "Q", "Q", "K", "K", "10", "10", "10", "Q", "Q", "R", "R", "J", "J", "9", "9", "F", "F", "10", "10", "Q", "Q", "J", "J", "K", "K", "9", "9", "10", "10", "10", "J", "J", "J", "R", "R", "10", "10", "Q", "Q", "Q", "F", "F", "9", "9", "Q", "Q", "B", "B", "9", "9", "R", "R", "Q", "Q", "Q", "K", "K", "J", "J", "B", "B", "B", "9", "9"},
    // Reel 3
    {"10", "10", "J", "J", "Q", "Q", "S", "9", "9", "K", "K", "F", "F", "B", "B", "R", "R", "10", "10", "Q", "Q", "10", "10", "9", "9", "9", "J", "J", "10", "10", "10", "K", "K", "J", "J", "9", "9", "K", "K", "J", "J", "J", "Q", "Q", "10", "10", "F", "F", "Q", "Q", "J", "J", "J", "9", "9", "9", "10", "10", "10", "K", "K", "B", "B", "J", "J", "Q", "Q", "K", "K", "B", "B", "10", "10", "F", "F", "Q", "Q", "R", "R", "B", "B", "J", "J", "F", "F", "Q", "Q", "10", "10", "R", "R", "9", "9", "F", "F", "J", "J", "K", "K", "Q", "Q", "10", "10", "9", "9"},
    // Reel 4
    {"9", "J", "10", "K", "R", "J", "B", "10", "9", "S", "R", "Q", "F", "K", "B", "Q", "F", "9", "10", "J", "Q", "B", "10", "K", "J", "9", "Q", "10", "F", "K", "9", "J", "10", "F", "9", "J", "B", "Q", "9", "R", "J", "Q", "F", "10", "9", "J", "R", "F", "9", "10", "J", "F", "9", "10", "R", "J", "B", "Q", "F", "J", "10", "Q", "9", "J", "10", "R", "9", "K", "J", "B", "10", "K", "9", "Q", "10", "J", "9", "K", "Q", "J", "10", "R", "9", "Q", "J", "R", "9", "K", "J", "Q", "9", "K", "R", "Q", "9", "10", "R", "Q", "9", "K", "R", "10", "9", "J", "R"},
    // Reel 5
    {"10", "9", "K", "F", "10", "Q", "J", "B", "10", "R", "S", "K", "10", "R", "F", "9", "J", "Q", "B", "10", "F", "J", "9", "K", "Q", "10", "J", "9", "F", "10", "Q", "K", "F", "J", "10", "Q", "9", "F", "10", "J", "9", "K", "R", "Q", "J", "10", "9", "K", "Q", "R", "J", "9", "10", "Q", "B", "J", "F", "9", "10", "J", "K", "Q", "10", "B", "Q", "9", "F", "10", "K", "Q", "B", "J", "10", "9", "F", "B", "10", "J", "K", "B", "9", "10", "K", "Q", "9", "R", "10", "J", "9", "R", "Q", "J", "9", "B", "F", "J", "9", "10", "Q", "F", "9", "J", "F", "10", "9"},
};

vector<vector<string>> reelsFGRefillStrAftX = {
    // Reel 1
    {"9", "9", "9", "10", "10", "J", "J", "Q", "Q", "K", "K", "B", "B", "F", "F", "R", "R", "9", "9", "J", "J", "J", "9", "9", "9", "Q", "Q", "10", "10", "J", "J", "K", "K", "10", "10", "B", "B", "9", "9", "9", "Q", "Q", "F", "F", "J", "J", "10", "10", "10", "R", "R", "F", "F", "K", "K", "B", "B", "R", "R", "9", "9", "Q", "Q", "10", "10", "10", "J", "J", "J", "10", "10", "9", "9", "9", "Q", "Q", "F", "F", "J", "J", "B", "B", "J", "J", "10", "10", "K", "K", "9", "9", "B", "B", "Q", "Q", "9", "9", "9", "K", "K", "10", "10", "K", "K", "Q", "Q"},
    // Reel 2
    {"Q", "Q", "J", "J", "S", "9", "9", "10", "10", "B", "B", "K", "K", "R", "R", "F", "F", "Q", "Q", "9", "9", "J", "J", "J", "Q", "Q", "B", "B", "B", "J", "J", "K", "K", "9", "9", "10", "10", "J", "J", "B", "B", "Q", "Q", "K", "K", "10", "10", "10", "Q", "Q", "R", "R", "J", "J", "9", "9", "F", "F", "10", "10", "Q", "Q", "J", "J", "K", "K", "9", "9", "10", "10", "10", "J", "J", "J", "R", "R", "10", "10", "Q", "Q", "Q", "F", "F", "9", "9", "Q", "Q", "B", "B", "9", "9", "R", "R", "Q", "Q", "Q", "K", "K", "J", "J", "B", "B", "B", "9", "9"},
    // Reel 3
    {"10", "10", "J", "J", "Q", "Q", "S", "9", "9", "K", "K", "F", "F", "B", "B", "R", "R", "10", "10", "Q", "Q", "10", "10", "9", "9", "9", "J", "J", "10", "10", "10", "K", "K", "J", "J", "9", "9", "K", "K", "J", "J", "J", "Q", "Q", "10", "10", "F", "F", "Q", "Q", "J", "J", "J", "9", "9", "9", "10", "10", "10", "K", "K", "B", "B", "J", "J", "Q", "Q", "K", "K", "B", "B", "10", "10", "F", "F", "Q", "Q", "R", "R", "B", "B", "J", "J", "F", "F", "Q", "Q", "10", "10", "R", "R", "9", "9", "F", "F", "J", "J", "K", "K", "Q", "Q", "10", "10", "9", "9"},
    // Reel 4
    {"9", "J", "10", "Q", "9", "10", "J", "Q", "S", "9", "R", "Q", "10", "9", "J", "K", "B", "9", "Q", "10", "J", "F", "B", "10", "K", "R", "J", "10", "F", "9", "K", "J", "B", "9", "Q", "J", "F", "9", "R", "Q", "J", "10", "F", "9", "J", "10", "B", "R", "F", "J", "10", "9", "R", "F", "J", "9", "K", "10", "F", "J", "9", "10", "B", "J", "9", "R", "F", "10", "9", "J", "B", "Q", "9", "K", "10", "J", "Q", "R", "10", "J", "K", "9", "Q", "R", "J", "K", "Q", "9", "R", "K", "Q", "J", "9", "K", "Q", "R", "10", "9", "Q", "R", "K", "9", "10", "R", "Q"},
    // Reel 5
    {"10", "9", "J", "Q", "10", "F", "9", "J", "K", "Q", "10", "J", "F", "R", "10", "K", "B", "J", "9", "10", "F", "Q", "9", "10", "B", "Q", "9", "10", "F", "Q", "9", "J", "F", "Q", "10", "K", "J", "F", "Q", "10", "9", "J", "Q", "K", "10", "9", "J", "Q", "R", "10", "J", "K", "9", "R", "Q", "K", "J", "9", "10", "Q", "B", "9", "K", "10", "B", "9", "Q", "K", "F", "9", "B", "10", "F", "J", "B", "10", "9", "J", "K", "10", "F", "R", "9", "K", "B", "10", "Q", "J", "9", "B", "R", "F", "9", "Q", "10", "J", "9", "F", "10", "J", "R", "K", "10", "J", "F"},
};

vector<vector<string>> reelsFG_END_SpinStr = {
    // Reel 1
    {"9", "9", "K", "K", "J", "J", "F", "F", "R", "R", "B", "B", "Q", "Q", "10", "10", "K", "K", "9", "9", "J", "J", "Q", "K", "K", "J", "J", "9", "9", "9", "B", "K", "K", "K", "R", "J", "J", "9", "9", "R", "K", "K", "9", "9", "F", "F", "K", "K", "B", "J", "J", "F", "F", "9", "9", "J", "J", "J", "Q", "9", "9", "9", "F", "F", "9", "9", "K", "K", "B", "J", "J", "9", "9", "J", "J", "9", "9", "K", "K", "F", "F", "9", "9", "K", "K", "J", "J", "F", "F", "K", "K", "J", "J", "F", "F", "K", "K", "J", "J", "10"},
    // Reel 2
    {"10", "10", "Q", "Q", "B", "B", "R", "R", "F", "F", "J", "J", "K", "K", "9", "9", "Q", "Q", "10", "10", "B", "B", "K", "Q", "Q", "B", "B", "10", "10", "10", "J", "Q", "Q", "Q", "F", "B", "B", "10", "10", "F", "Q", "Q", "10", "10", "R", "R", "Q", "Q", "J", "B", "B", "R", "R", "10", "10", "B", "B", "B", "K", "10", "10", "10", "R", "R", "10", "10", "Q", "Q", "J", "B", "B", "10", "10", "B", "B", "10", "10", "Q", "Q", "R", "R", "10", "10", "Q", "Q", "B", "B", "R", "R", "Q", "Q", "B", "B", "R", "R", "Q", "Q", "B", "B", "9"},
    // Reel 3
    {"9", "9", "F", "F", "9", "9", "Q", "Q", "9", "9", "J", "J", "F", "F", "10", "10", "B", "B", "R", "R", "K", "K", "B", "B", "9", "9", "F", "F", "10", "10", "R", "R", "J", "J", "Q", "Q", "9", "9", "10", "10", "K", "K", "Q", "Q", "F", "F", "J", "J", "B", "B", "R", "R", "10", "10", "9", "9", "R", "R", "K", "K", "J", "J", "10", "10", "F", "F", "Q", "Q", "J", "J", "K", "K", "B", "B", "10", "10", "F", "F", "Q", "Q", "9", "9", "R", "R", "K", "K", "J", "J", "10", "10", "9", "9", "F", "F", "10", "10", "Q", "Q", "R", "R"},
    // Reel 4
    {"9", "10", "J", "K", "9", "Q", "J", "10", "Q", "K", "R", "F", "9", "J", "R", "B", "9", "F", "10", "J", "B", "R", "9", "J", "B", "10", "9", "R", "J", "10", "K", "R", "J", "10", "Q", "9", "R", "10", "K", "9", "J", "10", "Q", "9", "J", "10", "Q", "B", "J", "10", "9", "Q", "J", "10", "9", "Q", "J", "R", "9", "10", "J", "Q", "9", "F", "J", "10", "Q", "9", "J", "10", "Q", "9", "J", "10", "9", "10", "9", "J", "Q", "9", "J", "9", "10", "9", "10", "Q", "9", "9", "J", "10", "J", "9", "Q", "9", "10", "J", "10", "J", "Q", "J"},
    // Reel 5
    {"K", "F", "J", "Q", "10", "B", "K", "9", "F", "J", "B", "Q", "K", "9", "10", "R", "K", "B", "10", "F", "9", "B", "R", "F", "10", "K", "9", "F", "J", "K", "R", "J", "Q", "10", "F", "R", "K", "9", "Q", "F", "10", "K", "Q", "9", "J", "F", "B", "Q", "R", "10", "J", "B", "R", "J", "9", "10", "K", "B", "9", "F", "Q", "K", "J", "R", "B", "K", "J", "Q", "R", "F", "J", "K", "B", "10", "F", "Q", "B", "9", "F", "Q", "10", "9", "R", "K", "J", "9", "B", "R", "K", "Q", "10", "J", "R", "F", "B", "9", "Q", "10", "F", "R"},
};

vector<vector<string>> reelsFG_END_RefillStr = reelsFG_END_SpinStr;

/**************
 * 字串輪帶轉符號輪帶
 **************/
uint8_t symCode(const string &s)
{
    if (s == "9")
        return S9;
    if (s == "10")
        return S10;
    if (s == "J")
        return SJ;
    if (s == "Q")
        return SQ;
    if (s == "K")
        return SK;
    if (s == "B")
        return SB;
    if (s == "F")
        return SF;
    if (s == "R")
        return SR;
    if (s == "W")
        return SW;
    if (s == "S")
        return SS;
    throw runtime_error("unknown symbol: " + s);
}

using Reels = vector<vector<uint8_t>>;

Reels packReels(const vector<vector<string>> &src)
{
    Reels dst(src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        dst[i].resize(src[i].size());
        for (size_t j = 0; j < src[i].size(); ++j)
        {
            dst[i][j] = symCode(src[i][j]);
        }
    }
    return dst;
}

Reels reelsMGSpin, reelsMGRefill;
Reels reelsFGSpin, reelsFGRefill;
Reels reelsMGRefillAftX, reelsFGRefillAftX;
Reels reelsFG_END_Spin, reelsFG_END_Refill;

/**************
 * 視窗
 **************/
struct window4x5
{
    uint8_t c[reelsCount][rows];
    int spinTopIdx[reelsCount];
    int refillTopIdx[reelsCount];
    bool refillSeeded[reelsCount];

    void spinInit(mt19937_64 &rng, const Reels &spinReels)
    {
        uniform_int_distribution<int> dist;
        for (int r = 0; r < reelsCount; ++r)
        {
            int L = (int)spinReels[r].size();
            dist = uniform_int_distribution<int>(0, L - 1);
            int stop = dist(rng);
            spinTopIdx[r] = stop;
            for (int row = 0; row < rows; ++row)
            {
                c[r][row] = spinReels[r][(stop + row) % L];
            }
            refillSeeded[r] = false;
        }
    }
};

void resetHybridRefillState(window4x5 &w)
{
    for (int r = 0; r < reelsCount; ++r)
    {
        w.refillSeeded[r] = false;
        w.refillTopIdx[r] = 0;
    }
}

/**************
 * Ways 計算
 **************/
array<int, reelsCount> reelMatchCounts(const window4x5 &w, uint8_t t)
{
    array<int, reelsCount> cnt{};
    for (int r = 0; r < reelsCount; ++r)
    {
        int k = 0;
        for (int row = 0; row < rows; ++row)
        {
            uint8_t s = w.c[r][row];
            if (s == SS)
                continue;
            if (s == t || s == SW)
                k++;
        }
        cnt[r] = k;
    }
    return cnt;
}

int maxLenForSymbol(const array<int, reelsCount> &cnt)
{
    int L = 0;
    for (int r = 0; r < reelsCount; ++r)
    {
        if (cnt[r] == 0)
            break;
        L++;
    }
    if (L > maxLen)
        L = maxLen;
    return L;
}

int waysForLength(const array<int, reelsCount> &cnt, int L)
{
    if (L < minPayLen)
        return 0;
    int w = 1;
    for (int r = 0; r < L; ++r)
        w *= cnt[r];
    return w;
}

struct Winner
{
    uint8_t sym;
    int len;
};

pair<double, vector<Winner>> evalWays(const window4x5 &w)
{
    double win = 0.0;
    vector<Winner> winners;
    for (uint8_t t = 0; t < NumSymbols; ++t)
    {
        if (t == SW || t == SS)
            continue;
        auto cnt = reelMatchCounts(w, t);
        int L = maxLenForSymbol(cnt);
        if (L < minPayLen)
            continue;
        if (L > 5)
            L = 5;
        int w5 = waysForLength(cnt, 5);
        int w4 = waysForLength(cnt, 4);
        int w3 = waysForLength(cnt, 3);
        if (w5 > 0)
        {
            win += (double)w5 * pay[t][2] * bet;
            winners.push_back({t, 5});
        }
        else if (w4 > 0)
        {
            win += (double)w4 * pay[t][1] * bet;
            winners.push_back({t, 4});
        }
        else if (w3 > 0)
        {
            win += (double)w3 * pay[t][0] * bet;
            winners.push_back({t, 3});
        }
    }
    return {win, winners};
}

/**************
 * 符號最長等級記錄
 **************/
int lenToIdx(int L)
{
    switch (L)
    {
    case 3:
        return 0;
    case 4:
        return 1;
    case 5:
        return 2;
    default:
        return -1;
    }
}

void bumpLenCats(const window4x5 &w, array<array<int64_t, 3>, NumSymbols> *dst)
{
    if (!dst)
        return;
    for (uint8_t t = 0; t < NumSymbols; ++t)
    {
        if (t == SW || t == SS)
            continue;
        auto cnt = reelMatchCounts(w, t);
        int L = maxLenForSymbol(cnt);
        if (L > 5)
            L = 5;
        int idx = lenToIdx(L);
        if (idx >= 0)
        {
            (*dst)[t][idx]++;
        }
    }
}

/**************
 * Cascades（Hybrid 補符）
 **************/
using Mark = bool[reelsCount][rows];

void markWinningCells(const window4x5 &w, const vector<Winner> &winners, Mark &mark)
{
    for (int r = 0; r < reelsCount; ++r)
        for (int row = 0; row < rows; ++row)
            mark[r][row] = false;

    for (auto &win : winners)
    {
        for (int r = 0; r < win.len; ++r)
        {
            for (int row = 0; row < rows; ++row)
            {
                uint8_t s = w.c[r][row];
                if (s == win.sym || s == SW)
                {
                    mark[r][row] = true;
                }
            }
        }
    }
}

pair<bool, array<int, reelsCount>> applyCascadesHybrid(
    mt19937_64 &rng,
    const Reels &refillReels,
    window4x5 &w,
    const vector<Winner> &winners)
{
    bool removedAny = false;
    array<int, reelsCount> removedByReel{};
    if (winners.empty())
        return {false, removedByReel};

    Mark mark;
    markWinningCells(w, winners, mark);

    uniform_int_distribution<int> dist;

    for (int r = 0; r < reelsCount; ++r)
    {
        vector<uint8_t> keep;
        keep.reserve(rows);
        for (int row = 0; row < rows; ++row)
        {
            if (!mark[r][row])
            {
                keep.push_back(w.c[r][row]);
            }
        }
        int removed = rows - (int)keep.size();
        removedByReel[r] = removed;
        if (removed > 0)
            removedAny = true;

        // 掉落
        for (int i = 0; i < (int)keep.size(); ++i)
        {
            w.c[r][rows - (int)keep.size() + i] = keep[i];
        }

        // 補符
        if (removed > 0)
        {
            int L = (int)refillReels[r].size();
            if (!w.refillSeeded[r])
            {
                dist = uniform_int_distribution<int>(0, L - 1);
                w.refillTopIdx[r] = dist(rng);
                w.refillSeeded[r] = true;
            }
            int newTop = (w.refillTopIdx[r] - removed) % L;
            if (newTop < 0)
                newTop += L;
            for (int row = 0; row < removed; ++row)
            {
                w.c[r][row] = refillReels[r][(newTop + row) % L];
            }
            w.refillTopIdx[r] = newTop;
        }
    }
    return {removedAny, removedByReel};
}

/**************
 * Scatter 計數
 **************/
int countScatterAll(const window4x5 &w)
{
    int c = 0;
    for (int r = 0; r < reelsCount; ++r)
    {
        for (int row = 0; row < rows; ++row)
        {
            if (w.c[r][row] == SS)
                c++;
        }
    }
    return c;
}

/**************
 * 初始/再觸發場次
 **************/
int startSpinsByScatter(int s)
{
    if (s < 3)
        return 0;
    int k = 8 + (s - 3) * 2;
    if (k > maxFGTotalSpins)
        k = maxFGTotalSpins;
    return k;
}
int retriggerByScatter(int s)
{
    if (s < 3)
        return 0;
    return 8 + (s - 3) * 2;
}

/**************
 * MG（含 AftX_MG）
 **************/
tuple<double, bool, bool, int, int> playMGSpin(
    mt19937_64 &rng,
    const Reels &spinReels,
    const Reels &refillReels,
    window4x5 &w,
    array<int64_t, 21> &mgComboHist,
    array<array<int64_t, 3>, NumSymbols> &mgInitLenCount,
    array<array<int64_t, 3>, NumSymbols> &mgLenCount)
{
    w.spinInit(rng, spinReels);

    const Reels *usedRefill = &refillReels;
    bool switched = false;

    double mult = 1.0;
    bool firstStep = true;
    double totalWin = 0.0;
    bool anyWin = false;
    int comboCnt = 0;

    uniform_real_distribution<double> uni01(0.0, 1.0);

    while (true)
    {
        auto [spinWin, winners] = evalWays(w);
        if (spinWin <= 0.0)
            break;

        if (firstStep)
        {
            bumpLenCats(w, &mgInitLenCount);
            firstStep = false;
        }
        bumpLenCats(w, &mgLenCount);

        anyWin = true;
        totalWin += spinWin * mult;
        comboCnt++;

        if (!switched && comboCnt >= AftComboX_MG)
        {
            if (uni01(rng) < pAftX_MG)
            {
                usedRefill = &reelsMGRefillAftX;
                resetHybridRefillState(w);
                switched = true;
            }
        }

        applyCascadesHybrid(rng, *usedRefill, w, winners);

        mult += 1.0;
        if (mult > 4.0)
            mult = 4.0;
    }

    int sAll = countScatterAll(w);
    bool trigFG = false;
    int fgStart = 0;
    if (sAll >= 3)
    {
        trigFG = true;
        fgStart = startSpinsByScatter(sAll);
    }

    int b = comboCnt;
    if (b > 20)
        b = 20;
    mgComboHist[b]++;

    return {totalWin, anyWin, trigFG, fgStart, comboCnt};
}

/**************
 * FG（含 AftX_FG、END 機制）
 **************/
struct FGRunResult
{
    int spins = 0;
    double total = 0.0;
    int retri = 0;
    int maxCombo = 0;
    int segPeak = 1;
    int64_t usedEndSpin = 0;
    int endRefillCutTurn = 0;
};

int nextFGMult(int current)
{
    if (current >= 50)
        return 50;
    if (current == 1)
        return 2;
    int n = current + 2;
    if (n > 50)
        n = 50;
    return n;
}

FGRunResult playFG(
    mt19937_64 &rng,
    const Reels &spinReels,
    const Reels &refillReels,
    window4x5 &w,
    int initSpins,
    array<int64_t, 21> &fgComboHistPerSpin,
    array<int64_t, 51> &peakMultHist,
    double &peakMultAvg,
    int &peakMultMax,
    array<int64_t, 21> &fgRetriggerDist,
    array<array<int64_t, 3>, NumSymbols> &fgInitLenCount,
    array<array<int64_t, 3>, NumSymbols> &fgLenCount)
{
    FGRunResult res;
    int queue = initSpins;
    int mult = 1;
    int segPeak = 1;

    int64_t usedEndSpinThisSeg = 0;
    int endRefillCutTurn = 0;
    bool hadEndRefill = false;

    enum
    {
        refillModeNormal = 0,
        refillModeAftX = 1,
        refillModeEnd = 2
    };

    uniform_real_distribution<double> uni01(0.0, 1.0);

    while (queue > 0)
    {
        queue--;
        res.spins++;
        int turnIndex = res.spins;

        const Reels *spinBand = &spinReels;
        if (mult >= aftMultX_FG_EndSpin && uni01(rng) < pEndSpin)
        {
            spinBand = &reelsFG_END_Spin;
            usedEndSpinThisSeg++;
        }
        w.spinInit(rng, *spinBand);

        int refillMode = refillModeNormal;
        int turnCombo = 0;
        bool firstStep = true;

        while (true)
        {
            auto [spinWin, winners] = evalWays(w);
            if (spinWin <= 0.0)
                break;

            if (firstStep)
            {
                bumpLenCats(w, &fgInitLenCount);
                firstStep = false;
            }
            bumpLenCats(w, &fgLenCount);

            res.total += (double)mult * spinWin;
            turnCombo++;

            if (mult >= aftMultX_FG_EndRefill)
            {
                if (refillMode != refillModeEnd)
                {
                    if (uni01(rng) < pEndRefill)
                    {
                        refillMode = refillModeEnd;
                        if (!hadEndRefill)
                        {
                            hadEndRefill = true;
                            endRefillCutTurn = turnIndex;
                        }
                        resetHybridRefillState(w);
                    }
                    else if (refillMode == refillModeNormal && uni01(rng) < pAftX_FG_AftMultX)
                    {
                        refillMode = refillModeAftX;
                        resetHybridRefillState(w);
                    }
                }
            }
            else
            {
                if (refillMode == refillModeNormal && turnCombo >= AftComboX_FG)
                {
                    if (uni01(rng) < pAftX_FG)
                    {
                        refillMode = refillModeAftX;
                        resetHybridRefillState(w);
                    }
                }
            }

            const Reels *usedRefill = nullptr;
            if (refillMode == refillModeNormal)
                usedRefill = &refillReels;
            else if (refillMode == refillModeAftX)
                usedRefill = &reelsFGRefillAftX;
            else
                usedRefill = &reelsFG_END_Refill;

            applyCascadesHybrid(rng, *usedRefill, w, winners);

            mult = nextFGMult(mult);
            if (mult > segPeak)
                segPeak = mult;
        }

        int s = countScatterAll(w);
        if (s >= 3)
        {
            int add = retriggerByScatter(s);
            int space = maxFGTotalSpins - (res.spins + queue);
            if (space < 0)
                space = 0;
            if (add > space)
                add = space;
            if (add > 0)
            {
                queue += add;
                res.retri++;
                int b = add;
                if (b > 20)
                    b = 20;
                fgRetriggerDist[b]++;
            }
        }

        int c = turnCombo;
        if (c > 20)
            c = 20;
        fgComboHistPerSpin[c]++;
        if (turnCombo > res.maxCombo)
            res.maxCombo = turnCombo;
    }

    if (segPeak < 1)
        segPeak = 1;
    if (segPeak > 50)
        segPeak = 50;
    peakMultHist[segPeak]++;
    peakMultAvg += segPeak;
    if (segPeak > peakMultMax)
        peakMultMax = segPeak;

    res.segPeak = segPeak;
    res.usedEndSpin = usedEndSpinThisSeg;
    res.endRefillCutTurn = endRefillCutTurn;
    return res;
}

/**************
 * 統計
 **************/
struct Stats
{
    double mainWinSum = 0.0;
    double freeWinSum = 0.0;
    int64_t triggerCount = 0;
    int64_t retriggerCount = 0;
    int64_t totalFGSpins = 0;
    double maxSingleSpin = 0.0;
    int64_t deadSpins = 0;
    int64_t mgHasWinCount = 0;

    double mgAvgCascades = 0.0;
    int mgComboMax = 0;
    array<int64_t, 21> mgComboHist{}; // 0..20

    int fgComboMax = 0;
    array<int64_t, 21> fgComboHist{}; // C=0..20

    array<int64_t, 51> fgStartDist{}; // 0..50

    double peakMultAvg = 0.0;
    int peakMultMax = 0;
    array<int64_t, 51> peakMultHist{}; // 0..50

    array<int64_t, 51> fgSegLenHist{};

    int64_t bigWins = 0;
    int64_t megaWins = 0;
    int64_t superWins = 0;
    int64_t holyWins = 0;
    int64_t jumboWins = 0;
    int64_t jojoWins = 0;

    array<int64_t, 21> retriggerDist{};

    int64_t endSpinUseCount = 0;
    array<int64_t, 51> endRefillCutTurnHist{};

    array<array<int64_t, 3>, NumSymbols> mgLenCount{};
    array<array<int64_t, 3>, NumSymbols> fgLenCount{};
    array<array<int64_t, 3>, NumSymbols> mgInitLenCount{};
    array<array<int64_t, 3>, NumSymbols> fgInitLenCount{};
};

/**************
 * 心跳
 **************/
atomic<int64_t> spinsDone{0};

string everyStr(int64_t totalSpins, int64_t count)
{
    if (count <= 0)
        return "（—）";
    long long v = llround((double)totalSpins / (double)count);
    return "（約每 " + to_string(v) + " 轉一次）";
}

void progressThread(int64_t total)
{
    auto start = chrono::steady_clock::now();
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(1));
        int64_t done = spinsDone.load();
        double elapsed = chrono::duration_cast<chrono::duration<double>>(chrono::steady_clock::now() - start).count();
        double speed = done / (elapsed + 1e-9);
        double eta = (total - done) / (speed + 1e-9);
        cerr << "[PROGRESS] " << done << "/" << total
             << " (" << (100.0 * done / total) << "%) | "
             << (long long)speed << " spins/s | ETA "
             << eta << "s\n";
        if (done >= total)
            break;
    }
}

/**************
 * Worker
 **************/
void workerFunc(int idx, int64_t spins, Stats &out, int64_t seed)
{
    mt19937_64 rng((uint64_t)seed);
    window4x5 w{};
    Stats local;
    double perSpinBet = bet;

    const int64_t bump = 4096;
    int64_t bumpCnt = 0;

    for (int64_t i = 0; i < spins; ++i)
    {
        auto [mgWin, mgAnyWin, trig, fgStart, mgCombo] = playMGSpin(
            rng, reelsMGSpin, reelsMGRefill, w,
            local.mgComboHist, local.mgInitLenCount, local.mgLenCount);
        local.mainWinSum += mgWin;
        if (mgAnyWin)
            local.mgHasWinCount++;
        local.mgAvgCascades += mgCombo;
        if (mgCombo > local.mgComboMax)
            local.mgComboMax = mgCombo;

        double spinTotal = mgWin;

        if (trig)
        {
            if (fgStart >= 8 && fgStart <= maxFGTotalSpins)
            {
                local.fgStartDist[fgStart]++;
            }
            FGRunResult res = playFG(
                rng,
                reelsFGSpin, reelsFGRefill,
                w, fgStart,
                local.fgComboHist,
                local.peakMultHist, local.peakMultAvg, local.peakMultMax,
                local.retriggerDist,
                local.fgInitLenCount, local.fgLenCount);
            local.freeWinSum += res.total;
            local.totalFGSpins += res.spins;
            local.retriggerCount += res.retri;
            local.triggerCount++;
            spinTotal += res.total;
            if (res.maxCombo > local.fgComboMax)
                local.fgComboMax = res.maxCombo;

            int segLen = res.spins;
            if (segLen < 1)
                segLen = 1;
            if (segLen > maxFGTotalSpins)
                segLen = maxFGTotalSpins;
            local.fgSegLenHist[segLen]++;

            local.endSpinUseCount += res.usedEndSpin;
            if (res.endRefillCutTurn > 0)
            {
                int t = res.endRefillCutTurn;
                if (t > maxFGTotalSpins)
                    t = maxFGTotalSpins;
                local.endRefillCutTurnHist[t]++;
            }
        }
        else if (!mgAnyWin)
        {
            local.deadSpins++;
        }

        double ratio = spinTotal / perSpinBet;
        if (ratio >= 1000.0)
            local.jojoWins++;
        else if (ratio >= 500.0)
            local.jumboWins++;
        else if (ratio >= 300.0)
            local.holyWins++;
        else if (ratio >= 100.0)
            local.superWins++;
        else if (ratio >= 60.0)
            local.megaWins++;
        else if (ratio >= 20.0)
            local.bigWins++;

        if (spinTotal > local.maxSingleSpin)
            local.maxSingleSpin = spinTotal;

        bumpCnt++;
        if (bumpCnt == bump)
        {
            spinsDone.fetch_add(bump);
            bumpCnt = 0;
        }
    }
    if (bumpCnt > 0)
        spinsDone.fetch_add(bumpCnt);
    out = local;
}

/**************
 * 輔助：符號標籤
 **************/
string symLabel(int t)
{
    switch (t)
    {
    case S9:
        return "9";
    case S10:
        return "10";
    case SJ:
        return "J";
    case SQ:
        return "Q";
    case SK:
        return "K";
    case SB:
        return "B";
    case SF:
        return "F";
    case SR:
        return "R";
    default:
        return "-";
    }
}

/**************
 * 主程式
 **************/
int main()
{
#ifdef _WIN32
    // 主控台改用 UTF-8，避免 中文/符號 亂碼（與 /utf-8 編譯搭配）
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    reelsMGSpin = packReels(reelsMGSpinStr);
    reelsMGRefill = packReels(reelsMGRefillStr);
    reelsFGSpin = packReels(reelsFGSpinStr);
    reelsFGRefill = packReels(reelsFGRefillStr);
    reelsMGRefillAftX = packReels(reelsMGRefillStrAftX);
    reelsFGRefillAftX = packReels(reelsFGRefillStrAftX);
    reelsFG_END_Spin = packReels(reelsFG_END_SpinStr);
    reelsFG_END_Refill = packReels(reelsFG_END_RefillStr);

    if (workers == 0)
        workers = 4;
    double totalBet = (double)numSpins * bet;

    thread prog(progressThread, numSpins);

    vector<thread> threads;
    vector<Stats> stats(workers);
    int64_t chunk = numSpins / (int64_t)workers;
    int64_t rem = numSpins % (int64_t)workers;
    int64_t baseSeed = chrono::steady_clock::now().time_since_epoch().count();

    for (unsigned w = 0; w < workers; ++w)
    {
        int64_t spins = chunk + (w < rem ? 1 : 0);
        threads.emplace_back(workerFunc, (int)w, spins, std::ref(stats[w]), baseSeed + (int64_t)w * 1337);
    }
    for (auto &th : threads)
        th.join();
    spinsDone.store(numSpins);
    prog.join();

    Stats total;
    array<int64_t, 21> totalRetriggerDist{};

    for (unsigned i = 0; i < workers; ++i)
    {
        const Stats &s = stats[i];
        total.mainWinSum += s.mainWinSum;
        total.freeWinSum += s.freeWinSum;
        total.triggerCount += s.triggerCount;
        total.retriggerCount += s.retriggerCount;
        total.totalFGSpins += s.totalFGSpins;
        if (s.maxSingleSpin > total.maxSingleSpin)
            total.maxSingleSpin = s.maxSingleSpin;
        total.deadSpins += s.deadSpins;
        total.mgHasWinCount += s.mgHasWinCount;

        total.mgAvgCascades += s.mgAvgCascades;
        if (s.mgComboMax > total.mgComboMax)
            total.mgComboMax = s.mgComboMax;
        for (int k = 0; k <= 20; ++k)
        {
            total.mgComboHist[k] += s.mgComboHist[k];
            total.fgComboHist[k] += s.fgComboHist[k];
        }
        if (s.fgComboMax > total.fgComboMax)
            total.fgComboMax = s.fgComboMax;
        for (int k = 1; k <= maxFGTotalSpins; ++k)
        {
            total.fgStartDist[k] += s.fgStartDist[k];
        }

        total.peakMultAvg += s.peakMultAvg;
        if (s.peakMultMax > total.peakMultMax)
            total.peakMultMax = s.peakMultMax;
        for (int k = 1; k <= 50; ++k)
        {
            total.peakMultHist[k] += s.peakMultHist[k];
        }
        for (int k = 1; k <= maxFGTotalSpins; ++k)
        {
            total.fgSegLenHist[k] += s.fgSegLenHist[k];
        }

        total.bigWins += s.bigWins;
        total.megaWins += s.megaWins;
        total.superWins += s.superWins;
        total.holyWins += s.holyWins;
        total.jumboWins += s.jumboWins;
        total.jojoWins += s.jojoWins;

        for (int k = 8; k <= 20; k += 2)
        {
            totalRetriggerDist[k] += s.retriggerDist[k];
        }

        total.endSpinUseCount += s.endSpinUseCount;
        for (int k = 1; k <= maxFGTotalSpins; ++k)
        {
            total.endRefillCutTurnHist[k] += s.endRefillCutTurnHist[k];
        }

        for (int t = 0; t < NumSymbols; ++t)
        {
            for (int b = 0; b < 3; ++b)
            {
                total.mgLenCount[t][b] += s.mgLenCount[t][b];
                total.fgLenCount[t][b] += s.fgLenCount[t][b];
                total.mgInitLenCount[t][b] += s.mgInitLenCount[t][b];
                total.fgInitLenCount[t][b] += s.fgInitLenCount[t][b];
            }
        }
    }

    double totalWin = total.mainWinSum + total.freeWinSum;
    double rtpMG = total.mainWinSum / totalBet;
    double rtpFG = total.freeWinSum / totalBet;
    double rtpTotal = totalWin / totalBet;

    // 印基本設定
    cout << "=== Monte Carlo | workers=" << workers
         << " | spins=" << numSpins
         << " | ways=" << numWays
         << " | bet=" << bet << " ===\n";
    cout << "AftX_MG   : AftComboX_MG=" << AftComboX_MG
         << ", pAftX_MG=" << pAftX_MG << "\n";
    cout << "AftX_FG   : AftComboX_FG=" << AftComboX_FG
         << ", pAftX_FG=" << pAftX_FG
         << ", pAftX_FG_AftMultX=" << pAftX_FG_AftMultX << "\n";
    cout << "END_Refill: aftMultX_FG_EndRefill=" << aftMultX_FG_EndRefill
         << ", pEndRefill=" << pEndRefill << "\n";
    cout << "END_Spin  : aftMultX_FG_EndSpin=" << aftMultX_FG_EndSpin
         << ", pEndSpin=" << pEndSpin << "\n";

    // 固定小數格式
    cout << fixed;

    // 金額與 RTP：小數 2 位
    cout << setprecision(2);
    cout << "\n";
    cout << "總成本 (Total Bet)                    : " << totalBet << "\n";
    cout << "總贏分 (Total Win)                    : " << totalWin << "\n";
    cout << "最高單把贏分                          : " << total.maxSingleSpin
         << " (x" << (total.maxSingleSpin / bet) << ")\n";

    cout << setprecision(5);
    cout << "主遊戲 RTP                            : " << rtpMG << "\n";
    cout << "免費遊戲 RTP                          : " << rtpFG << "\n";
    cout << "總 RTP                                : " << rtpTotal << "\n";

    cout << setprecision(6);
    cout << "免費遊戲觸發次數                      : " << total.triggerCount
         << " (觸發率 " << (double)total.triggerCount / (double)numSpins
         << ") " << everyStr(numSpins, total.triggerCount) << "\n";
    for (int k = 8; k <= 18; k += 2)
    {
        int64_t cnt = total.fgStartDist[k];
        cout << "  └起始 " << setw(2) << k << " 轉                         : "
             << setw(10) << cnt
             << " (機率 " << (double)cnt / (double)numSpins
             << ") " << everyStr(numSpins, cnt) << "\n";
    }

    cout << "免費遊戲再觸發次數                    : " << total.retriggerCount
         << " " << everyStr(numSpins, total.retriggerCount) << "\n";
    for (int k = 8; k <= 18; k += 2)
    {
        int64_t cnt = totalRetriggerDist[k];
        cout << "  └再觸發 +" << setw(2) << k << " 轉                      : "
             << setw(10) << cnt
             << " (機率 " << (double)cnt / (double)numSpins
             << ") " << everyStr(numSpins, cnt) << "\n";
    }

    if (total.triggerCount > 0)
    {
        cout << "每次免費遊戲平均場次                  : "
             << (double)total.totalFGSpins / (double)total.triggerCount << "\n";
    }

    cout << "MG 有贏分比例                         : "
         << (double)total.mgHasWinCount / (double)numSpins
         << " " << everyStr(numSpins, total.mgHasWinCount) << "\n";
    cout << "MG 無贏分且無觸發 FG                  : " << total.deadSpins
         << " (空轉占比 "
         << (double)total.deadSpins / (double)numSpins << ")\n";

    cout << "MG 最高連消次數（combo）              : " << total.mgComboMax << "\n";

    int64_t fgSpinCount = 0;
    for (int c = 0; c <= 20; ++c)
        fgSpinCount += total.fgComboHist[c];

    cout << "FG 最高連消次數（combo）              : " << total.fgComboMax << "\n";

    double peakAvg = 0.0;
    if (total.triggerCount > 0)
    {
        peakAvg = total.peakMultAvg / (double)total.triggerCount;
    }
    cout << "FG 平均累計倍率（每段）               : " << peakAvg << "\n";
    cout << "FG 最高累計倍率（每段）—（以觸發次數為分母）\n";
    for (int k = 1; k <= 50; ++k)
    {
        int64_t cnt = total.peakMultHist[k];
        if (cnt == 0)
            continue;
        double p = (total.triggerCount > 0)
                       ? (double)cnt / (double)total.triggerCount
                       : 0.0;
        cout << "  peak=" << setw(2) << k
             << " : " << setw(10) << cnt
             << "  (" << p << ")\n";
    }

    cout << "\n每段 FG 的實際總場次（以觸發次數為分母）上限 : "
         << maxFGTotalSpins << "\n";
    for (int k = 1; k <= maxFGTotalSpins; ++k)
    {
        int64_t cnt = total.fgSegLenHist[k];
        if (cnt == 0)
            continue;
        double p = (total.triggerCount > 0)
                       ? (double)cnt / (double)total.triggerCount
                       : 0.0;
        cout << "  len=" << setw(2) << k
             << " : " << setw(10) << cnt
             << "  (" << p << ")\n";
    }

    int64_t mgGE4 = 0;
    for (int c = 4; c <= 20; ++c)
        mgGE4 += total.mgComboHist[c];
    cout << "\nMG 連消≥4 次數   : " << mgGE4
         << " (占比 " << (double)mgGE4 / (double)numSpins
         << ") " << everyStr(numSpins, mgGE4) << "\n";

    cout << "MG 連消次數分佈（每把） C=0..20，>20 併入 20：\n";
    for (int c = 0; c <= 20; ++c)
    {
        int64_t cnt = total.mgComboHist[c];
        double p = (double)cnt / (double)numSpins;
        cout << "  C=" << setw(2) << c
             << "  : " << setw(10) << cnt
             << "  (" << p << ") "
             << everyStr(numSpins, cnt) << "\n";
    }

    cout << "\nFG 連消次數分佈（逐轉） C=0..20，>20 併入 20：\n";
    if (fgSpinCount == 0)
    {
        for (int c = 0; c <= 20; ++c)
        {
            cout << "  C=" << setw(2) << c
                 << "  : 0  (0.000000) （—）\n";
        }
    }
    else
    {
        for (int c = 0; c <= 20; ++c)
        {
            int64_t cnt = total.fgComboHist[c];
            double p = (double)cnt / (double)fgSpinCount;
            cout << "  C=" << setw(2) << c
                 << "  : " << setw(10) << cnt
                 << "  (" << p << ") "
                 << everyStr(fgSpinCount, cnt) << "\n";
        }
    }

    cout << "\nreelsFG_END_Spin 使用次數              : " << total.endSpinUseCount << "\n";
    cout << "切入 reelsFG_END_Refill 的時機（FG 段的第幾轉）\n";
    for (int k = 1; k <= maxFGTotalSpins; ++k)
    {
        int64_t cnt = total.endRefillCutTurnHist[k];
        if (cnt == 0)
            continue;
        cout << "  第 " << setw(2) << k << " 轉              : " << cnt << "\n";
    }

    cout << "\n獎項分佈\n";
    cout << "Big  Win (≥  20×bet)                  : " << total.bigWins
         << " (占比 " << (double)total.bigWins / (double)numSpins << ") "
         << everyStr(numSpins, total.bigWins) << "\n";
    cout << "Mega Win (≥  60×bet)                  : " << total.megaWins
         << " (占比 " << (double)total.megaWins / (double)numSpins << ") "
         << everyStr(numSpins, total.megaWins) << "\n";
    cout << "Super Win(≥ 100×bet)                  : " << total.superWins
         << " (占比 " << (double)total.superWins / (double)numSpins << ") "
         << everyStr(numSpins, total.superWins) << "\n";
    cout << "Holy Win (≥ 300×bet)                  : " << total.holyWins
         << " (占比 " << (double)total.holyWins / (double)numSpins << ") "
         << everyStr(numSpins, total.holyWins) << "\n";
    cout << "Jumbo Win(≥ 500×bet)                  : " << total.jumboWins
         << " (占比 " << (double)total.jumboWins / (double)numSpins << ") "
         << everyStr(numSpins, total.jumboWins) << "\n";
    cout << "Jojo Win (≥1000×bet)                  : " << total.jojoWins
         << " (占比 " << (double)total.jojoWins / (double)numSpins << ") "
         << everyStr(numSpins, total.jojoWins) << "\n";

    cout << "\nMG 初轉各符號 3/4/5 連次數\n";
    for (int t : {S9, S10, SJ, SQ, SK, SB, SF, SR})
    {
        auto c3 = total.mgInitLenCount[t][0];
        auto c4 = total.mgInitLenCount[t][1];
        auto c5 = total.mgInitLenCount[t][2];
        cout << "  " << symLabel(t)
             << "  : 3連=" << setw(10) << c3
             << "  4連=" << setw(10) << c4
             << "  5連=" << setw(10) << c5 << "\n";
    }

    cout << "\nMG 因消除新增各符號 3/4/5 連次數（總計 − 初轉）\n";
    for (int t : {S9, S10, SJ, SQ, SK, SB, SF, SR})
    {
        auto a3 = total.mgLenCount[t][0] - total.mgInitLenCount[t][0];
        auto a4 = total.mgLenCount[t][1] - total.mgInitLenCount[t][1];
        auto a5 = total.mgLenCount[t][2] - total.mgInitLenCount[t][2];
        cout << "  " << symLabel(t)
             << "  : 3連=" << setw(10) << a3
             << "  4連=" << setw(10) << a4
             << "  5連=" << setw(10) << a5 << "\n";
    }

    cout << "\nMG 各符號 3/4/5 連次數（含初轉 + 消除）\n";
    for (int t : {S9, S10, SJ, SQ, SK, SB, SF, SR})
    {
        auto c3 = total.mgLenCount[t][0];
        auto c4 = total.mgLenCount[t][1];
        auto c5 = total.mgLenCount[t][2];
        cout << "  " << symLabel(t)
             << "  : 3連=" << setw(10) << c3
             << "  4連=" << setw(10) << c4
             << "  5連=" << setw(10) << c5 << "\n";
    }

    cout << "\nFG 初轉各符號 3/4/5 連次數\n";
    for (int t : {S9, S10, SJ, SQ, SK, SB, SF, SR})
    {
        auto c3 = total.fgInitLenCount[t][0];
        auto c4 = total.fgInitLenCount[t][1];
        auto c5 = total.fgInitLenCount[t][2];
        cout << "  " << symLabel(t)
             << "  : 3連=" << setw(10) << c3
             << "  4連=" << setw(10) << c4
             << "  5連=" << setw(10) << c5 << "\n";
    }

    cout << "\nFG 因消除新增各符號 3/4/5 連次數（總計 − 初轉）\n";
    for (int t : {S9, S10, SJ, SQ, SK, SB, SF, SR})
    {
        auto a3 = total.fgLenCount[t][0] - total.fgInitLenCount[t][0];
        auto a4 = total.fgLenCount[t][1] - total.fgInitLenCount[t][1];
        auto a5 = total.fgLenCount[t][2] - total.fgInitLenCount[t][2];
        cout << "  " << symLabel(t)
             << "  : 3連=" << setw(10) << a3
             << "  4連=" << setw(10) << a4
             << "  5連=" << setw(10) << a5 << "\n";
    }

    cout << "\nFG 各符號 3/4/5 連次數（含初轉 + 消除）\n";
    for (int t : {S9, S10, SJ, SQ, SK, SB, SF, SR})
    {
        auto c3 = total.fgLenCount[t][0];
        auto c4 = total.fgLenCount[t][1];
        auto c5 = total.fgLenCount[t][2];
        cout << "  " << symLabel(t)
             << "  : 3連=" << setw(10) << c3
             << "  4連=" << setw(10) << c4
             << "  5連=" << setw(10) << c5 << "\n";
    }

    return 0;
}
