# Regex

从零实现正则表达式引擎，并按照实现演进保留三个独立的 C++17 程序：

| 源码 | 内容 | 对应文章 |
|------|------|----------|
| `src/regex-0.cc` | 递归回溯 Matcher | [正则表达式引擎：基础篇](regex-engine-1.md) |
| `src/regex-1.cc` | Parser → AST → NFA → DFA | [正则表达式引擎：基础篇](regex-engine-1.md) |
| `src/regex-2.cc` | 字符集合、谓词边、等价类、Lazy DFA 与文本分段 | [正则表达式引擎：高级篇](regex-engine-2.md) |

`regex-0` 用简短代码展示直接匹配；`regex-1` 展示经典的正则编译流水线；`regex-2` 面向 Tokenizer 预分词 Pattern，将单字符转移推广为字符集合，并通过 `CharPred`、字符等价类和 Lazy DFA 支持 Unicode 文本分段。

## 构建与运行

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/regex-0
./build/regex-1
./build/regex-2
```

三个程序均包含内置示例和检查。`regex-1`、`regex-2` 以 Unicode codepoint 处理 UTF-8 输入。

## 文档

- [正则表达式引擎：基础篇](regex-engine-1.md)：从递归 Matcher 讲到 Parser、AST、Thompson NFA、子集构造和 DFA 匹配。
- [正则表达式引擎：高级篇](regex-engine-2.md)：围绕目标预分词 Pattern，讲解字符集合、谓词化 NFA、字符等价类、Lazy DFA 和 `Segment`。

## License

MIT
