# Command : pip install pandas matplotlib
import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib as mpl
import matplotlib.ticker as mtick

# ========= 基本設定 =========
BASE_PATH = "./csv"   # 來源路徑
OUT_PATH  = "./image" # 輸出路徑

# 中文字型
mpl.rcParams["font.sans-serif"] = ["Microsoft JhengHei", "SimHei", "Arial Unicode MS"]
mpl.rcParams["axes.unicode_minus"] = False

# 眼睛痛
def _apply_dark_theme(fig, ax):
    bg = "#222222"
    fg = "#ffffff"
    fig.patch.set_facecolor(bg)
    ax.set_facecolor(bg)

    for spine in ax.spines.values():
        spine.set_color(fg)

    ax.tick_params(colors=fg)

    ax.xaxis.label.set_color(fg)
    ax.yaxis.label.set_color(fg)
    ax.title.set_color(fg)


def _ensure_outdir():
    os.makedirs(OUT_PATH, exist_ok=True)


# ========== 1. MG combo ==========
def plot_mg_combo_hist():
    df = pd.read_csv(os.path.join(BASE_PATH, "mg_combo_hist.csv"))
    _ensure_outdir()
    fig, ax = plt.subplots(figsize=(8, 4), dpi=200)
    ax.bar(df["combo"], df["ratio"])
    ax.set_xlabel("MG combo 次數（C）")
    ax.set_ylabel("佔\n比", rotation=0)
    ax.set_title("MG 連消次數分佈（每把）")
    ax.set_xticks(df["combo"])
    ax.yaxis.set_label_coords(-0.07, 0.5)
    ax.yaxis.set_major_locator(mtick.MultipleLocator(0.05))
    ax.yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0, decimals=0))
    _apply_dark_theme(fig, ax)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_PATH, "mg_combo_hist.png"), dpi=300)
    plt.close(fig)


# ========== 2. FG combo ==========
def plot_fg_combo_hist():
    df = pd.read_csv(os.path.join(BASE_PATH, "fg_combo_hist.csv"))
    _ensure_outdir()
    fig, ax = plt.subplots(figsize=(8, 4), dpi=200)
    ax.bar(df["combo"], df["ratio"])
    ax.set_xlabel("FG combo 次數（C）")
    ax.set_ylabel("佔\n比", rotation=0)
    ax.set_title("FG 連消次數分佈（逐轉）")
    ax.set_xticks(df["combo"])
    ax.yaxis.set_label_coords(-0.07, 0.5)
    ax.yaxis.set_major_locator(mtick.MultipleLocator(0.05))
    ax.yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0, decimals=0))
    _apply_dark_theme(fig, ax)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_PATH, "fg_combo_hist.png"), dpi=300)
    plt.close(fig)


# ========== 3. 每段 FG 實際總場次分佈 ==========
def plot_fg_segment_length_hist():
    df = pd.read_csv(os.path.join(BASE_PATH, "fg_segment_length_hist.csv"))
    _ensure_outdir()
    fig, ax = plt.subplots(figsize=(8, 4), dpi=200)
    ax.bar(df["len"], df["prob_per_trigger"])
    ax.set_xlabel("每段 FG 實際總場次（轉數）")
    ax.set_ylabel("佔\n比", rotation=0)
    ax.set_title("FG 段長分佈（以觸發次數為分母）")
    ticks = list(range(8, 51, 2))
    ax.set_xticks(ticks)
    ax.yaxis.set_label_coords(-0.07, 0.5)
    ax.yaxis.set_major_locator(mtick.MultipleLocator(0.05))
    ax.yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0, decimals=0))
    _apply_dark_theme(fig, ax)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_PATH, "fg_segment_length_hist.png"), dpi=300)
    plt.close(fig)


# ========== 4. FG 段落峰值倍率分佈 ==========
def plot_fg_peak_mult_hist():
    df = pd.read_csv(os.path.join(BASE_PATH, "fg_peak_mult_hist.csv"))
    _ensure_outdir()
    fig, ax = plt.subplots(figsize=(8, 4), dpi=200)
    ax.bar(df["multiplier"], df["prob_per_trigger"])
    ax.set_xlabel("本段 FG 的最高累計倍率（peak mult）")
    ax.set_ylabel("佔\n比", rotation=0)
    ax.set_title("FG 段落峰值倍率分佈")
    ticks = [1, 2] + list(range(4, 51, 2))
    ax.set_xticks(ticks)
    ax.yaxis.set_label_coords(-0.07, 0.5)
    ax.yaxis.set_major_locator(mtick.MultipleLocator(0.02))
    ax.yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0, decimals=0))
    _apply_dark_theme(fig, ax)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_PATH, "fg_peak_mult_hist.png"), dpi=300)
    plt.close(fig)


# ========= 3/4/5 連的共用函式 =========
def _plot_symbol_len_ratio(df, col, title, filename):
    """
    df: mg_symbol_len_counts.csv or fg_symbol_len_counts.csv
    col: "len3_count", "len4_count", "len5_count"
    title: 圖表標題字串
    filename: 圖片輸出檔名（含 .png）
    """
    _ensure_outdir()

    fig, ax = plt.subplots(figsize=(8, 4), dpi=200)

    total = df[col].sum()
    df["ratio"] = df[col] / total

    ax.bar(df["symbol"], df["ratio"])

    ax.set_xlabel("符號")
    ax.set_ylabel("佔\n比", rotation=0)
    ax.set_title(title)

    # y 軸百分比
    ax.yaxis.set_label_coords(-0.09, 0.5)
    ax.yaxis.set_major_locator(mtick.MultipleLocator(0.025))
    ax.yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0, decimals=1))

    _apply_dark_theme(fig, ax)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_PATH, filename), dpi=300)
    plt.close(fig)


# ========= 5.1 ~ 5.6： MG/FG 3/4/5 連 =========

def plot_mg_len3():
    df = pd.read_csv(os.path.join(BASE_PATH, "mg_symbol_len_counts.csv"))
    _plot_symbol_len_ratio(df, "len3_count", "MG 各符號 3連比例", "mg_len3.png")

def plot_mg_len4():
    df = pd.read_csv(os.path.join(BASE_PATH, "mg_symbol_len_counts.csv"))
    _plot_symbol_len_ratio(df, "len4_count", "MG 各符號 4連比例", "mg_len4.png")

def plot_mg_len5():
    df = pd.read_csv(os.path.join(BASE_PATH, "mg_symbol_len_counts.csv"))
    _plot_symbol_len_ratio(df, "len5_count", "MG 各符號 5連比例", "mg_len5.png")

def plot_fg_len3():
    df = pd.read_csv(os.path.join(BASE_PATH, "fg_symbol_len_counts.csv"))
    _plot_symbol_len_ratio(df, "len3_count", "FG 各符號 3連比例", "fg_len3.png")

def plot_fg_len4():
    df = pd.read_csv(os.path.join(BASE_PATH, "fg_symbol_len_counts.csv"))
    _plot_symbol_len_ratio(df, "len4_count", "FG 各符號 4連比例", "fg_len4.png")

def plot_fg_len5():
    df = pd.read_csv(os.path.join(BASE_PATH, "fg_symbol_len_counts.csv"))
    _plot_symbol_len_ratio(df, "len5_count", "FG 各符號 5連比例", "fg_len5.png")


# ========= 主程式 =========
def main():

    plot_mg_combo_hist()
    plot_fg_combo_hist()
    plot_fg_segment_length_hist()
    plot_fg_peak_mult_hist()
    plot_mg_len3()
    plot_mg_len4()
    plot_mg_len5()
    plot_fg_len3()
    plot_fg_len4()
    plot_fg_len5()

    print("已輸出圖檔：")
    print("  mg_combo_hist.png")
    print("  fg_combo_hist.png")
    print("  fg_segment_length_hist.png")
    print("  fg_peak_mult_hist.png")
    print("  mg_len3.png  mg_len4.png  mg_len5.png")
    print("  fg_len3.png  fg_len4.png  fg_len5.png")


if __name__ == "__main__":
    main()
