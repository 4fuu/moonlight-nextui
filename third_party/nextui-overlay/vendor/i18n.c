#include "i18n.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

typedef struct
{
    const char* key;
    const char* zh;
} I18nEntry;

// English keys keep the source text stable and make a missing translation
// obvious at the call site. Only entries that differ in Chinese belong here;
// English-only or intentionally untranslated strings simply omit the entry.
static const I18nEntry zhTable[] = {
    // Launcher: root entries, hints and messages.
    {"Recently Played", "最近游玩"},
    {"Recents", "最近"},
    {"Collections", "收藏"},
    {"Tools", "工具"},
    {"Games", "游戏"},
    {"No Preview", "无预览"},
    {"No Recents", "暂无最近记录"},
    {"Empty folder", "空文件夹"},
    {"Disc %i", "光盘 %i"},
    {"BACK", "返回"},
    {"OPEN", "打开"},
    {"RESUME", "继续"},
    {"REMOVE", "移除"},
    {"SLEEP", "休眠"},
    {"INFO", "信息"},

    // Settings: top-level entries, shared hints and the Language row.
    {"Appearance", "外观"},
    {"UI customization", "界面自定义"},
    {"Display", "显示"},
    {"System", "系统"},
    {"FN switch", "FN 开关"},
    {"FN switch settings", "FN 开关设置"},
    {"Assignments", "按键分配"},
    {"Customize button assignments", "自定义按键功能"},
    {"Network", "网络"},
    {"Bluetooth", "蓝牙"},
    {"About", "关于"},
    {"OKAY", "确定"},
    {"Language", "语言"},
    {"The interface language. Takes effect after returning to the desktop.", "界面显示语言。返回桌面后生效。"},
    {"Simplified Chinese", "简体中文"},

    // Settings: Appearance page labels, hints and values.
    {"Font", "字体"},
    {"The font to render all UI text.", "用于呈现界面文字的字体。"},
    {"Font style", "字体样式"},
    {"The style to render the UI font (e.g. bold)", "界面字体样式（例如加粗）"},
    {"Bold", "加粗"},
    {"Color Palette", "调色板"},
    {"Custom", "自定义"},
    {"Pick a predefined color palette or edit the colors below.", "选择预设调色板，或在下方修改各项颜色。"},
    {"Main Color", "主色"},
    {"The color used to render main UI elements.", "主要界面元素所用的颜色。"},
    {"Primary Accent Color", "主强调色"},
    {"The color used to highlight important things in the user interface.", "用于突出显示界面中重要信息的颜色。"},
    {"Secondary Accent Color", "次强调色"},
    {"A secondary highlight color.", "次要的强调颜色。"},
    {"List Text", "列表文字"},
    {"List text color", "列表文字颜色。"},
    {"List Text Selected", "列表选中文字"},
    {"List selected text color", "列表选中文字的颜色。"},
    {"Hint info Color", "提示信息颜色"},
    {"Color for button hints and info", "按钮提示与信息的颜色。"},
    {"Background Color", "背景颜色"},
    {"Background color used when no background image is set.", "未设置背景图片时使用的背景颜色。"},
    {"Show battery percentage", "显示电池百分比"},
    {"Show battery level as percent in the status pill", "在状态栏中以百分比显示电量。"},
    {"Show menu animations", "显示菜单动画"},
    {"Enable or disable menu animations", "启用或禁用菜单动画。"},
    {"Menu transitions", "菜单过渡"},
    {"Style of animated transition when navigating menus", "菜单切换时的过渡动画风格。"},
    {"Off", "关"},
    {"On", "开"},
    {"Snappy", "轻快"},
    {"Comfy", "舒缓"},
    {"Show folder names at root", "根目录显示文件夹名"},
    {"Show folder names at root directory", "在根目录中显示文件夹名称。"},
    {"Show Recents", "显示最近游玩"},
    {"Show \"Recently Played\" menu entry in game list.", "在游戏列表中显示“最近游玩”入口。"},
    {"Show Tools", "显示工具"},
    {"Show \"Tools\" menu entry in game list.", "在游戏列表中显示“工具”入口。"},
    {"Use folder background for ROMs", "ROM 使用文件夹背景"},
    {"If enabled, used the emulator background image. Otherwise uses the default.", "启用时使用模拟器背景图片，否则使用默认背景。"},
    {"Show Quickswitcher UI", "显示切换器界面"},
    {"Show/hide Quickswitcher UI elements.\nWhen hidden, will only draw background images.", "显示或隐藏切换器界面元素，隐藏时仅绘制背景图片。"},
    {"Game switcher curtain opacity", "切换器遮罩不透明度"},
    {"Show/hide curtain overlay. Helps UI elements to \nstand out when using transparent backgrounds.", "显示或隐藏遮罩层，使界面元素在透明背景下更醒目。"},
    {"Input prompt style", "按键提示样式"},
    {"Select the style of input prompts.", "选择按键提示的样式。"},
    {"Text", "文字"},
    {"Cardinals", "方向"},
    {"Shapes", "形状"},

    // Settings: color picker button hints.
    {"APPLY", "应用"},
    {"FINE", "微调"},
    {"COARSE", "粗调"},
    {"COPY", "复制"},

    // Settings: About page labels and messages ("About" itself is shared with
    // the root menu and lives in the top-level section above).
    {"NextUI version", "NextUI 版本"},
    {"Platform", "平台"},
    {"Stock OS version", "原厂系统版本"},
    {"Busybox version", "BusyBox 版本"},
    {"BusyBox version not found.", "未找到 BusyBox 版本。"},
    // The About page's "NextUI version" row value, shown when .system/version.txt
    // is missing or empty. The literal lives in settings/version.cpp as
    // VERSION_FALLBACK and is returned by readVersionText(); settings.cpp routes
    // only that fallback through TR(), so a real version string is untouched.
    {"Unknown", "未知"},
    {"Stock OS changes detected.\n"
     "This may cause instability or issues.\n"
     "If you experience problems, please consider\n"
     "reverting to clean stock firmware.",
     "检测到原厂系统被修改。\n"
     "这可能导致不稳定或其他问题。\n"
     "如果遇到问题，请考虑\n"
     "恢复为干净的原厂固件。"},

    // Settings: shared button hints (menu.cpp overlays, keyboardprompt) and the
    // on-screen keyboard's special key captions.
    {"OK", "确定"},
    {"EXIT", "退出"},
    {"ENTER", "回车"},
    {"shift", "上档"},
    {"space", "空格"},
    {"enter", "回车"},

    // Settings: Network (WiFi) page labels, hints and values.
    {"WiFi", "WiFi"},
    {"Enable/disable WiFi", "启用或禁用 WiFi。"},
    {"WiFi diagnostics", "WiFi 诊断"},
    {"Enable/disable WiFi logging", "启用或禁用 WiFi 日志记录。"},
    {"Enabling WiFi...", "正在启用 WiFi..."},
    {"Disabling WiFi...", "正在禁用 WiFi..."},
    {"Options", "选项"},
    {"Disconnect", "断开连接"},
    {"Disconnect from this network.", "断开与此网络的连接。"},
    {"Connect", "连接"},
    {"Connect to this network.", "连接到此网络。"},
    {"Forget", "忘记"},
    {"Removes credentials for this network.", "删除此网络的凭据。"},
    {"Enter WiFi passcode", "输入 WiFi 密码"},
    {"Enter Wifi passcode", "输入 WiFi 密码"},
    {"Connecting...", "正在连接..."},

    // Settings: Bluetooth page labels, hints and values. The page's MenuList
    // title is "Network" in the source (a copy/paste from the WiFi menu); the
    // English string is left as it is, so the shared "Network" key above covers
    // it and the Chinese title reads the same as the WiFi page until the label
    // is fixed. Reported rather than changed.
    {"Enable/disable Bluetooth", "启用或禁用蓝牙。"},
    {"Bluetooth diagnostics", "蓝牙诊断"},
    {"Enable/disable Bluetooth logging", "启用或禁用蓝牙日志记录。"},
    {"Maximum sampling rate", "最高采样率"},
    {"44100 Hz: better compatibility\n48000 Hz: better quality", "44100 Hz：兼容性更好；48000 Hz：音质更好"},
    {"Enabling Bluetooth...", "正在启用蓝牙..."},
    {"Disabling Bluetooth...", "正在禁用蓝牙..."},
    // Settings: Assignments (FN button) page labels, hints and values. The
    // button name itself is hardware ("L3", "HOME", ...) and stays as it is;
    // only the generic word is translated, so the key keeps its leading space.
    {"None", "无"},
    {"Exit to desktop", "退出到桌面"},
    {"System overlay", "系统浮层"},
    {" button", " 按键"},
    {"Action for this button; pak actions open from the menu.", "此按键的功能；选择 pak 后可从菜单中打开。"},
    {" (missing)", "（缺失）"},

    {"Pair", "配对"},
    {"Pair this device.", "配对此设备。"},
    {"Pairing...", "正在配对..."},
    {"Forget this device.", "忘记此设备。"},
    {"Connect this device.", "连接此设备。"},
    {"Disconnect this device.", "断开此设备的连接。"},

    // Settings: Display page labels and hints.
    {"Brightness", "亮度"},
    {"Display brightness (0 to 10)", "显示亮度（0 到 10）"},
    {"Color temperature", "色温"},
    {"Color temperature (0 to 40)", "色温（0 到 40）"},
    {"Contrast", "对比度"},
    {"Contrast enhancement (-4 to 5)", "对比度增强（-4 到 5）"},
    {"Saturation", "饱和度"},
    {"Saturation enhancement (-5 to 5)", "饱和度增强（-5 到 5）"},
    {"Exposure", "曝光"},
    {"Exposure enhancement (-4 to 5)", "曝光增强（-4 到 5）"},
    {"White point correction", "白点校正"},
    {"Corrects the display white point to better match the \nsRGB standard, at the expense of some peak brightness.", "校正显示白点，使其更接近 sRGB 标准，但会牺牲一定的峰值亮度。"},
    {"Red gain", "红色增益"},
    {"White point correction red channel gain (0 to 200)", "白点校正红色通道增益（0 到 200）"},
    {"Green gain", "绿色增益"},
    {"White point correction green channel gain (0 to 200)", "白点校正绿色通道增益（0 到 200）"},
    {"Blue gain", "蓝色增益"},
    {"White point correction blue channel gain (0 to 200)", "白点校正蓝色通道增益（0 到 200）"},
    {"Reset to defaults", "恢复默认设置"},
    {"Resets all options in this menu to their default values.", "将本菜单中的所有选项恢复为默认值。"},

    // Settings: sound rows (System page volume, FN switch page mute volume).
    {"Volume", "音量"},
    {"Speaker volume", "扬声器音量"},
    {"Volume when toggled", "切换时音量"},
    {"Speaker volume (0-20)", "扬声器音量（0-20）"},

    // Settings: light row (FN switch page LED behavior).
    {"FN switch disables LED", "FN 开关关闭 LED"},
    {"Switch will also disable LEDs", "该开关同时关闭 LED。"},

    // Settings: System page labels and hints.
    // Hints are single-line on purpose: the font subset has no U+000A glyph, so a
    // zh value cannot carry the English "\n", and GFX_blitText draws each line at
    // its natural width without wrapping. Keep every hint under ~1000 px at
    // FONT_TINY on the 1024 px desktop reference (the narrowest target).
    {"Screen timeout", "屏幕超时"},
    {"Period of inactivity before screen turns off (0-600s)", "无操作多久后关闭屏幕（0-600秒）"},
    {"Suspend timeout", "休眠超时"},
    {"Time before device goes to sleep after screen is off (5-600s)", "屏幕关闭后多久进入休眠（5-600秒）"},
    {"Haptic feedback", "振动反馈"},
    {"Enable or disable haptic feedback on certain actions in the OS", "启用或禁用系统中特定操作的振动反馈"},
    {"Default view", "默认视图"},
    {"The initial view to show on boot", "开机时显示的初始界面"},
    {"Show 24h time format", "24小时制"},
    {"Show clock in the 24hrs time format", "以24小时制显示时钟"},
    {"Show clock", "显示时钟"},
    {"Show clock in the status pill", "在状态栏中显示时钟"},
    {"Set time and date automatically", "自动设置时间和日期"},
    {"Automatically adjust system time\nwith NTP (requires internet access)", "通过网络时间协议（NTP）自动调整系统时间（需要联网）"},
    {"Time zone", "时区"},
    {"Your time zone", "您所在的时区"},
    {"Safe poweroff", "安全关机"},
    {"Bypasses the stock shutdown procedure to avoid the \"limbo bug\".\nInstructs the PMIC directly to soft disconnect the battery.", "绕过原厂关机流程以避免“limbo bug”，直接指示 PMIC 软断开电池。"},
    {"Keep awake over USB", "USB 连接时保持唤醒"},
    {"Prevent screen-off and sleep while connected to a\ncomputer as a USB device (not just charging).", "作为 USB 设备连接电脑时（不仅是充电）防止息屏和休眠。"},
    {"Fan Speed", "风扇转速"},
    // Halfwidth parens here: the key ends in "%)", and the i18n test requires the
    // zh value to carry the same printf signature, so the ")" must stay ASCII.
    {"Select the fan speed percentage (Quiet/Normal/Performance or 0-100%)", "选择风扇转速百分比(安静/标准/性能或 0-100%)"},

    // Settings: System page value labels (shared with the FN switch volume rows
    // where noted; the shared on_off labels are translated through
    // translateLabels at menu build time).
    {"Muted", "静音"},
    {"Content List", "内容列表"},
    {"Game Switcher", "游戏切换器"},
    {"Quick Menu", "快捷菜单"},
    {"Performance", "性能"},
    {"Normal", "标准"},
    {"Quiet", "安静"},
    {"Never", "从不"},
    {"5s", "5秒"},
    {"10s", "10秒"},
    {"15s", "15秒"},
    {"30s", "30秒"},
    {"45s", "45秒"},
    {"60s", "1分钟"},
    {"90s", "90秒"},
    {"2m", "2分钟"},
    {"4m", "4分钟"},
    {"6m", "6分钟"},
    {"10m", "10分钟"},

    // Settings: FN switch page labels and hints (the "when toggled" hints reuse
    // the Display page keys above).
    {"Brightness when toggled", "切换时亮度"},
    {"Color temperature when toggled", "切换时色温"},
    {"Contrast when toggled", "切换时对比度"},
    {"Saturation when toggled", "切换时饱和度"},
    {"Exposure when toggled", "切换时曝光"},
    {"Turbo fire A", "连发 A"},
    {"Enable turbo fire A", "启用 A 连发"},
    {"Turbo fire B", "连发 B"},
    {"Enable turbo fire B", "启用 B 连发"},
    {"Turbo fire X", "连发 X"},
    {"Enable turbo fire X", "启用 X 连发"},
    {"Turbo fire Y", "连发 Y"},
    {"Enable turbo fire Y", "启用 Y 连发"},
    {"Turbo fire L1", "连发 L1"},
    {"Enable turbo fire L1", "启用 L1 连发"},
    {"Turbo fire L2", "连发 L2"},
    {"Enable turbo fire L2", "启用 L2 连发"},
    {"Turbo fire R1", "连发 R1"},
    {"Enable turbo fire R1", "启用 R1 连发"},
    {"Turbo fire R2", "连发 R2"},
    {"Enable turbo fire R2", "启用 R2 连发"},
    {"Dpad mode when toggled", "切换时十字键模式"},
    {"Dpad: default. Joystick: Dpad exclusively acts as analog stick.\nBoth: Dpad and Joystick inputs at the same time.", "十字键：默认；摇杆：仅作模拟摇杆；两者：可同时输入。"},

    // Settings: FN switch page value labels.
    {"Unchanged", "不更改"},
    {"Dpad", "十字键"},
    {"Joystick", "摇杆"},
    {"Both", "两者"},

    // Settings: System Update page - menu entry, row labels, action buttons,
    // in-flight prompts, and every honest status/error. Keys are the TR() source
    // strings; the About page's "Unknown" version-fallback key is shared.
    {"System Update", "系统更新"},
    {"Check, download, and install a system update", "检查、下载并安装系统更新"},
    {"Current version", "当前版本"},
    {"Status", "状态"},
    {"Ready", "就绪"},
    {"Check for updates", "检查更新"},
    {"Download update", "下载更新"},
    {"Install update", "安装更新"},
    {"Look for a newer system update.", "查找更新的系统更新。"},
    {"Download and verify the update into staging.", "下载并验证更新到暂存区。"},
    {"Install the downloaded update, then restart to finish.", "安装已下载的更新，然后重启完成。"},
    {"Checking for updates...", "正在检查更新..."},
    {"Downloading update...", "正在下载更新..."},
    {"Installing update...", "正在安装更新..."},
    {"An interrupted update will be resolved on the next restart.", "有未完成的更新，将在下次重启时处理。"},
    {"Update available", "有可用更新"},
    {"Your system is up to date.", "系统已是最新。"},
    {"Ready to install", "可安装"},
    {"Update installed. Restart to apply.", "更新已安装，重启后生效。"},
    {"That update is already installed.", "该更新已安装。"},
    {"Update source is not configured.", "未配置更新源。"},
    {"Could not reach the update source.", "无法连接更新源。"},
    {"This update requires a newer system.", "此更新需要更高的系统版本。"},
    {"Update verification failed. Nothing was installed.", "更新验证失败，未安装任何内容。"},
    {"Another operation is in progress.", "另一操作正在进行。"},
    {"The downloaded update is older than your system.", "下载的更新比当前系统旧。"},
    {"Not enough space to download the update.", "空间不足，无法下载更新。"},
    {"System update tool is not available.", "系统更新工具不可用。"},
    {"Download an update first.", "请先下载更新。"},
    {"This update is not for this device.", "此更新不适用于本设备。"},
    {"Cannot determine the current system version.", "无法确定当前系统版本。"},
    {"The system installation is missing.", "系统安装缺失。"},
    {"This package is not a system update.", "此包不是系统更新。"},
    {"Update failed.", "更新失败。"},

    // Settings: "Install from file" page - the menu entry, the row labels, the
    // action button, the in-flight prompt, the row reasons for a package that
    // cannot be installed, and every honest status/error. Keys are the TR()
    // source strings; the trusted-key contract is shared with the System Update
    // page, which has no key message of its own because it never installs.
    {"Install from file", "从文件安装"},
    {"Install an app package from the SD card", "从存储卡安装应用包"},
    {"Package folder", "安装包目录"},
    {"Look for packages", "重新扫描安装包"},
    {"Scan the package folder again.", "重新扫描安装包目录。"},
    {"No trusted key is configured.", "未配置可信公钥。"},
    {"No package found.", "没有找到安装包。"},
    {"Choose a package to install.", "选择要安装的安装包。"},
    {"App package", "应用包"},
    {"Installing app...", "正在安装应用..."},
    {"Looking for packages...", "正在扫描安装包..."},
    {"Installed", "已安装"},
    {"That version is already installed.", "该版本已经安装。"},
    {"The package is older than the installed app.", "安装包比已安装的应用更旧。"},
    {"This app requires a newer system.", "此应用需要更新的系统版本。"},
    {"Verification failed. Nothing was installed.", "验证失败，未安装任何内容。"},
    {"Not enough space to install.", "空间不足，无法安装。"},
    {"Update tool is not available.", "更新工具不可用。"},
    {"This package is not for this device.", "此安装包不适用于本设备。"},
    {"The installed app version cannot be read.", "无法读取已安装应用的版本。"},
    {"The package is missing its manifest file.", "安装包缺少清单文件。"},
    {"The package is missing its signature file.", "安装包缺少签名文件。"},
    {"The package manifest cannot be read.", "无法读取安装包清单。"},
    {"This package is not an application package.", "此安装包不是应用包。"},
    {"That package is no longer in the folder.", "该安装包已不在目录中。"},
    {"This package cannot be installed.", "此安装包无法安装。"},
    {"Install failed.", "安装失败。"},

    // Shared hardware overlay hints (common/api.c GFX_blitHardwareHints): the
    // brightness and color temperature pills, plus the MENU/SELECT combo hints
    // shown while the volume pill is up. The button labels ("+ -", "MNU",
    // "SEL") are hardware names and stay English, matching the repo convention;
    // only the hint words are translated. "BRGHT"/"CLTMP" are the combo-pill
    // abbreviations of the two full hints above and share their translations,
    // which are two characters and therefore narrower than the English.
    {"BRIGHTNESS", "亮度"},
    {"COLOR TEMP", "色温"},
    {"BRGHT", "亮度"},
    {"CLTMP", "色温"},

    // First-batch built-in Tools: launcher labels. The pak directory and the
    // pak.json name stay English on disk; nextui.c translates only this fixed
    // set of display names, so a third-party pak is never renamed.
    {"Clock", "时钟"},
    {"Settings", "设置"},
    {"LedControl", "灯光控制"},
    {"Battery", "电池"},
    {"Input", "按键测试"},
    {"Bootlogo", "开机画面"},
    {"Files", "文件"},
    {"Remove Loading", "移除加载画面"},

    // Clock: time-format toggle and its button hints.
    {"12 HOUR", "12 小时制"},
    {"24 HOUR", "24 小时制"},
    {"CANCEL", "取消"},
    {"SET", "设定"},

    // Battery: title, the zoom windows in it, the four stats rows and the
    // initial "no data yet" value.
    {"Battery usage: Last %s", "电池用量：最近 %s"},
    {"16 hours", "16 小时"},
    {"8 hours", "8 小时"},
    {"4 hours", "4 小时"},
    {"Since Charge: %s", "本次充电：%s"},
    {"Current: %s", "当前：%s"},
    {"Remaining: %s", "预计剩余：%s"},
    {"Longest: %s", "最长续航：%s"},
    {"calculating", "计算中"},
    {"SCROLL", "滚动"},
    {"ZOOM", "缩放"},

    // Input test: the one non-hardware label on the screen.
    {"QUIT", "退出"},

    // Bootlogo: the empty-preset message and its apply hint ("SCROLL" and
    // "SET" are shared with Battery and Clock above).
    {"No presets found in\n%s", "未找到预设位置：\n%s"},

    // Shared UIOverlay copy. Apps pass I18N_overlayStrings() so the side panel
    // speaks the same language as the rest of the interface; keys that already
    // exist above (System, Bluetooth, Exit to desktop, Ready, On, Off) are
    // intentionally not repeated. "Wi-Fi" reads the same in both languages.
    {"Resume", "继续"},
    {"Save state", "保存进度"},
    {"Close", "关闭"},
    {"Select", "选择"},
    {"No saved state", "无存档"},
    {"No changes", "无更改"},
    {"Unavailable", "不可用"},
    {"Not connected", "未连接"},
    {"Saved", "已保存"},
    {"Resumed", "已继续"},
    {"Failed", "失败"},
};

#define ZH_TABLE_COUNT ((int)(sizeof(zhTable) / sizeof(zhTable[0])))

// Log each distinct missing key at most once. This keeps an untranslated
// label from filling the log every frame while still making gaps visible.
#define MISSING_LOG_MAX 64
static const char* missingLogged[MISSING_LOG_MAX];
static int missingLoggedCount = 0;

static void logMissing(const char* key)
{
    for (int i = 0; i < missingLoggedCount; i++)
    {
        if (missingLogged[i] == key || strcmp(missingLogged[i], key) == 0)
            return;
    }

    if (missingLoggedCount >= MISSING_LOG_MAX)
        return;

    missingLogged[missingLoggedCount++] = key;
    printf("[I18N] missing zh translation: %s\n", key);
}

const char* I18N_translate(const char* key)
{
    if (!key || !key[0])
        return key ? key : "";

    if (strcmp(CFG_getLanguage(), "zh") != 0)
        return key;

    for (int i = 0; i < ZH_TABLE_COUNT; i++)
    {
        if (strcmp(zhTable[i].key, key) == 0)
            return zhTable[i].zh;
    }

    logMissing(key);
    return key;
}

UIOverlayStrings I18N_overlayStrings(void)
{
    UIOverlayStrings strings = {
        .title = TR("System"),
        .resume = TR("Resume"),
        .save = TR("Save state"),
        // "Wi-Fi" reads the same in both languages; skipping TR also keeps the
        // missing-translation log free of an intentional identity value.
        .wifi = "Wi-Fi",
        .bluetooth = TR("Bluetooth"),
        .exit_to_desktop = TR("Exit to desktop"),
        .close = TR("Close"),
        .select = TR("Select"),
        .ready = TR("Ready"),
        .no_state = TR("No saved state"),
        .no_changes = TR("No changes"),
        .unavailable = TR("Unavailable"),
        .not_connected = TR("Not connected"),
        .on = TR("On"),
        .off = TR("Off"),
        .saved = TR("Saved"),
        .resumed = TR("Resumed"),
        .failed = TR("Failed"),
    };
    return strings;
}
