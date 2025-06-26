# 深入 X11：理解 Motif Window Manager Hints (`_MOTIF_WM_HINTS`)

X11 是一个高度模块化、可配置的窗口系统，它本身并不强制窗口的行为和外观，而是将这些责任交给窗口管理器（Window Manager）。为了让应用程序能与窗口管理器更好地协作，X11 提供了一套“Hints”机制来传达窗口的意图和需求。

其中，**Motif Window Manager Hints** 是最广泛应用的非官方扩展之一，尤其用于控制窗口的外观（比如是否显示标题栏）、行为（是否可以移动、关闭、最大化等）以及输入模式等。

本文将带你深入理解 `_MOTIF_WM_HINTS` 属性中的几个关键字段，探索其实际用途和实现方法。

---

## `_MOTIF_WM_HINTS` 是什么？

这是一个自定义的 X11 属性（property），最早由 Motif 窗口管理器（MWM）引入，后来被众多窗口管理器所继承支持，比如：

- Metacity / Mutter（GNOME）
- KWin（KDE）
- Openbox
- IceWM
- xfwm4（Xfce）
- … 以及许多嵌入式/定制窗口管理器

该属性允许应用程序通过设置某些提示值，控制：

- 窗口允许的操作（如最小化、关闭）
- 窗口装饰的外观（是否显示边框/标题栏）
- 输入模式（是否为模态对话框）
- 其他状态信息

它的结构如下（C语言伪代码）：

```c
typedef struct {
    uint32_t flags;
    uint32_t functions;
    uint32_t decorations;
    int32_t  input_mode;
    uint32_t status;
} MotifWmHints;
```

---

## flags 字段

该字段是一个位掩码（bitmask），用于指示哪些字段是有效的。通常，你会看到如下宏定义：

```c
#define MWM_HINTS_FUNCTIONS    (1L << 0)
#define MWM_HINTS_DECORATIONS  (1L << 1)
#define MWM_HINTS_INPUT_MODE   (1L << 2)
#define MWM_HINTS_STATUS       (1L << 3)
```

比如：

- `flags = MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS` 表示你要同时设置功能（`functions`）和装饰（`decorations`）字段。

---

## functions 字段（功能控制）

当 `MWM_HINTS_FUNCTIONS` 位被设置时，窗口管理器将读取 `functions` 字段的值来决定用户可以对窗口执行哪些操作。

支持的操作有：

```c
#define MWM_FUNC_ALL       (1L << 0)  // 支持所有操作
#define MWM_FUNC_RESIZE    (1L << 1)  // 支持调整大小
#define MWM_FUNC_MOVE      (1L << 2)  // 支持移动
#define MWM_FUNC_MINIMIZE  (1L << 3)  // 支持最小化
#define MWM_FUNC_MAXIMIZE  (1L << 4)  // 支持最大化
#define MWM_FUNC_CLOSE     (1L << 5)  // 支持关闭
```

例如：
如果你只希望窗口支持“移动”和“关闭”：

```c
hints.flags = MWM_HINTS_FUNCTIONS;
hints.functions = MWM_FUNC_MOVE | MWM_FUNC_CLOSE;
```

---

## decorations 字段（窗口装饰控制）

当 `MWM_HINTS_DECORATIONS` 位被设置时，可以使用该字段来控制窗口的视觉外观，特别是标题栏和边框的绘制。

它的常用值与 `functions` 类似，实际上一些窗口管理器可能会忽略该值，仅使用以下：

```c
#define MWM_DECOR_ALL      (1L << 0)  // 默认所有装饰
#define MWM_DECOR_BORDER   (1L << 1)  // 边框
#define MWM_DECOR_RESIZEH  (1L << 2)  // 支持调整大小
#define MWM_DECOR_TITLE    (1L << 3)  // 标题栏
#define MWM_DECOR_MENU     (1L << 4)  // 菜单
#define MWM_DECOR_MINIMIZE (1L << 5)  // 最小化按钮
#define MWM_DECOR_MAXIMIZE (1L << 6)  // 最大化按钮
```

最常见的用途是完全去掉所有装饰（无边框窗口）：

```c
hints.flags = MWM_HINTS_DECORATIONS;
hints.decorations = 0; // 没有任何装饰
```

---

## input_mode 字段（输入模式）

设置该字段需要在 `flags` 中指定 `MWM_HINTS_INPUT_MODE`。

这个字段主要用于对话框窗口，决定窗口在输入焦点和堆叠顺序方面的表现：

```c
#define MWM_INPUT_MODELESS          0         // 非模态
#define MWM_INPUT_PRIMARY_APPLICATION_MODAL 1 // 模态
#define MWM_INPUT_SYSTEM_MODAL      2         // 系统模态
#define MWM_INPUT_FULL_APPLICATION_MODAL    3 // 少见
```

常见场景是你希望创建一个模态对话框：

```c
hints.flags = MWM_HINTS_INPUT_MODE;
hints.input_mode = MWM_INPUT_PRIMARY_APPLICATION_MODAL;
```

---

## status 字段（状态）

这个字段几乎没有被实际使用，大多数窗口管理器会忽略它。即便 `MWM_HINTS_STATUS` 被设置，行为也并无明显区别，因此一般不使用。

---

## 实际设置方法（Xlib 示例）

```c
Atom prop = XInternAtom(display, "_MOTIF_WM_HINTS", False);

MotifWmHints hints = {0};
hints.flags = MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS;
hints.functions = MWM_FUNC_MOVE | MWM_FUNC_CLOSE;
hints.decorations = 0; // 无边框

XChangeProperty(display, window, prop, prop, 32,
                PropModeReplace, (unsigned char *)&hints, 5);
```

---

## 使用场景举例

- **无边框全屏窗口**（比如游戏或视频播放器）：
  ```c
  hints.flags = MWM_HINTS_DECORATIONS;
  hints.decorations = 0;
  ```

- **禁用关闭按钮的模态对话框**：
  ```c
  hints.flags = MWM_HINTS_FUNCTIONS | MWM_HINTS_INPUT_MODE;
  hints.functions = MWM_FUNC_MOVE;
  hints.input_mode = MWM_INPUT_PRIMARY_APPLICATION_MODAL;
  ```

- **强制只允许最小化**（不常见，但可以这样做）：
  ```c
  hints.flags = MWM_HINTS_FUNCTIONS;
  hints.functions = MWM_FUNC_MINIMIZE;
  ```

---

## 注意事项

- **不是所有窗口管理器都100%遵循这些提示**，但大多数主流窗口管理器都对 `_MOTIF_WM_HINTS` 有良好支持。
- 在现代工具包中（如 GTK、Qt），你通常无需手动设置这些属性，可以通过对应 API（如 Qt 的 `setWindowFlags()`）间接设置。
- 对于 Electron 应用或嵌入式窗口管理器的定制，直接操控 `_MOTIF_WM_HINTS` 是控制窗口行为的常用技巧。

---

## 结语

`_MOTIF_WM_HINTS` 虽然并非 X11 的官方核心协议，但它在整个 X11 生态中被广泛采纳，是窗口行为控制的重要工具。掌握这些低层细节，将帮助你打造更具用户体验、行为一致性的 X11 应用窗口。
