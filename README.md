# Regex

从零实现正则表达式引擎，包含两个实现：

1. **NaiveRegex** — 基于递归回溯的简单匹配器，支持 `.`、`*`、`?`、`\d` 等基础语法
2. **Regex** — 完整的正则引擎，经典编译流程：
   - 递归下降解析器 → AST
   - Thompson 构造：AST → NFA
   - 子集构造：NFA → DFA
   - DFA 匹配

支持 UTF-8，可处理中文和 Emoji。

详细的实现原理和代码讲解见 [Regex.md](Regex.md)。

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 运行

完整正则引擎（含构建过程输出和测试）：

```bash
./build/regex
```

简单匹配器测试：

```bash
./build/naive_regex
```

## 支持的语法

| 语法 | 说明 | 示例 |
|------|------|------|
| 字面量 | 匹配字符本身 | `abc` |
| `.` | 匹配任意字符（换行除外） | `a.c` |
| `\|` | 或 | `a\|b` |
| `*` | 零次或多次 | `a*` |
| `+` | 一次或多次 | `a+` |
| `?` | 零次或一次 | `a?` |
| `()` | 分组 | `(ab)*` |

NaiveRegex 额外支持：`\d`（数字）、`\a`（字母）、`\s`（空白）、`^`（行首）、`$`（行尾）等。

## License

MIT
