# UTF-8 文本哈夫曼压缩实验

面向数据结构课程的 C++17 教学项目。程序以 Unicode 码点为编码符号，实现 UTF-8 文本的字符频率统计、哈夫曼树构建、压缩、解压及逐字节无损验证，可以直接观察汉字的出现频率与哈夫曼编码的关系。

## 功能

- 压缩合法 UTF-8 文本，解压后恢复原始字节序列。
- 查看各字符的出现次数、编码和码长；空格、换行等不可见字符使用标签显示。
- 支持汉字、英文、数字、标点、空白和 UTF-8 编码占 4 字节的字符。
- 处理空文件、单一字符文件，保留 CR/LF、TAB 和 BOM。
- 提供交互菜单与命令行，显示压缩率及压缩、解压耗时。
- 比较原文件与还原文件的长度和每个字节，验证无损还原。

这里的“字符”指 Unicode 码点。组合字符和组合 Emoji 可能包含多个码点；程序分别编码各码点，不进行规范化或换行转换。输入不合法时报告错误，不替换字符，也不自动转换 GBK、UTF-16 等其他编码。

## 项目结构

```text
huffman-utf8/
├── README.md
├── .gitignore
├── .gitattributes             # 保留示例文件的原始字节
├── code/
│   ├── huffman_utf8.cpp       # 主程序
│   ├── samples/
│   │   ├── demo.txt           # 中中中AB，无末尾换行
│   │   ├── demo_codes.txt     # 示例频率及编码表
│   │   └── mixed.txt          # 混合文本及四字节字符
│   └── tests/
│       ├── test_utf8.py       # 文件往返及独立参考编码验证
│       └── core_checks.cpp    # Unicode 转换及树权值验证
└── docs/
    └── images/
        ├── workflow.png       # 压缩与解压示意图
        └── file-format.png    # 压缩文件组织示意图
```

## 编译与运行

需要支持 C++17 的编译器。以下命令在 `code` 目录执行。

### Windows MinGW

```powershell
cd code
g++ -std=c++17 -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8 huffman_utf8.cpp -o huffman_utf8.exe -lshell32
.\huffman_utf8.exe
```

Windows 下使用 `-lshell32` 链接命令行路径处理所需的库。可使用 Dev-C++、Code::Blocks，或已配置编译器和调试器的 VS Code，工程编译标准设为 C++17。

### Linux

```bash
cd code
g++ -std=c++17 -O2 huffman_utf8.cpp -o huffman_utf8
./huffman_utf8
```

### 命令行示例

Windows：

```powershell
.\huffman_utf8.exe t samples\demo.txt
.\huffman_utf8.exe c samples\demo.txt demo.huf8
.\huffman_utf8.exe d demo.huf8 restored.txt
.\huffman_utf8.exe v samples\demo.txt restored.txt
```

Linux：

```bash
./huffman_utf8 t samples/demo.txt
./huffman_utf8 c samples/demo.txt demo.huf8
./huffman_utf8 d demo.huf8 restored.txt
./huffman_utf8 v samples/demo.txt restored.txt
```

| 命令 | 功能 | 参数 |
|---|---|---|
| `c` | 压缩 | 原始文本、压缩文件 |
| `d` | 解压 | 压缩文件、还原文本 |
| `v` | 比较文件 | 原始文件、还原文件 |
| `t` | 显示频率与编码表 | 原始文本 |
| `--help` | 显示帮助 | 无 |

不带参数时进入菜单：1 压缩、2 解压、3 比较文件、4 查看频率及编码表、0 退出。输出文件必须使用未存在的新文件名；含空格的路径用双引号包围。操作成功返回 0，失败或文件比较不一致返回 1，命令参数错误返回 2。

## 压缩与解压思路

![UTF-8 文本压缩与解压流程](docs/images/workflow.png)

压缩依次完成以下操作：

1. 以二进制方式读取文件，解析合法的 UTF-8 字节序列，获得 Unicode 码点序列。
2. 统计各码点的出现次数，建立按码点升序排列的字符频率表。
3. 将出现次数作为权值构建哈夫曼树，由树生成字符编码表。
4. 在文件头保存字符总数和频率表，按原文顺序查找编码表，将编码位打包后写入数据区。

**压缩端先建树、再生成编码，写文件时直接复用这些结果。文件头保存频率表，而不是另存整棵树或编码表。**

解压读取文件头中的频率表，按相同规则重建哈夫曼树，再从根结点逐位遍历：0 进入左子树，1 进入右子树，到达叶结点后输出对应字符的 UTF-8 字节。达到原始字符总数时结束，不将末尾补零解释成额外字符。沿树译码无需另建编码表。

为使两端重建出相同的树，叶结点按码点升序编号，新结点依次编号，同权时优先选择编号较小者，先选者作为左孩子；左分支为 0，右分支为 1。

### 主要结构与函数

| 环节 | 结构或函数 |
|---|---|
| 文件读取与 UTF-8 解析 | `ReadUTF8Text`、`DecodeUTF8`、`std::u32string` |
| 字符频率统计 | `CountFrequency`、`FrequencyTable`、`std::map` |
| 哈夫曼树构建 | `HTNode`、`HuffmanTree`、`Select`、`CreateHuffmanTree` |
| 编码生成 | `HuffmanCode`、`HuffmanCoding` |
| 字符定位与位流打包 | `LocateCharacter`、`EncodeData`、`WriteBit`、`FlushBits` |
| 文件头读取与译码 | `ReadHeader`、`DecodeData`、`EncodeUTF8` |
| 字节一致性验证 | `CompareFiles` |

核心采用结构体、从 1 开始的动态数组、双亲及左右孩子下标和独立函数，便于在数据结构课程中阅读和调试。本项目为原创教学实现。`Select` 使用顺序扫描，m 种字符的建树复杂度为 O(m²)；程序会将原文及解析后的码点序列读入内存。

## 压缩文件格式

![字符压缩文件组织](docs/images/file-format.png)

示例扩展名为 `.huf8`。所有整数采用小端序，文件依次包含：

| 字段 | 长度 | 含义 |
|---|---:|---|
| 原始字符总数 | 8 字节 | 无符号 64 位，统计码点数 |
| 字符种类数 m | 4 字节 | 无符号 32 位 |
| 字符频率表 | 8m 字节 | 每条记录为 4 字节码点和 4 字节出现次数 |
| 编码数据区 | 可变 | 编码位连续打包为字节 |

频率表只记录实际出现的字符，按码点升序保存。读取 m 条记录后即可定位数据区，起始偏移为 `12 + 8m`。数据位按高位到低位存放，最后不足 8 位时在低位补零。

- 空文件：字符总数和种类数均为 0，没有编码数据区。
- 单一字符：编码约定为 `0`，按原始字符总数恢复。
- 单个字符的出现次数使用 32 位无符号整数，合并权值使用 64 位无符号整数；超出支持范围时报告错误。
- 格式不含校验和或版本标记。程序检查频率表、截断数据、补零和多余数据，还原结果通过 `v` 命令逐字节验证。
- 本格式与旧版“固定 256 项字节频率表”的格式不兼容。

## 示例 中中中AB

`code/samples/demo.txt` 恰为 `中中中AB`，无末尾换行。原文为 11 个 UTF-8 字节、5 个码点、3 种字符。

| 字符 | 出现次数 | 哈夫曼编码 |
|---|---:|---|
| A | 1 | `00` |
| B | 1 | `01` |
| 中 | 3 | `1` |

原文转换后的编码位流为 `1110001`，在低位补零后得到 `11100010`，编码数据占 1 字节。文件头占 `12 + 8 × 3 = 36` 字节，完整压缩文件为 37 字节。这个短文本用于说明算法，文件头开销使它压缩后更大。

压缩率按完整压缩文件字节数计算：

```text
压缩率 = 压缩文件字节数 / 原文件字节数 × 100%
```

按此定义，数值越小，压缩效果越好；大于 100% 表示文件变大，空文件不计算。实验可比较汉字重复较多、字符种类较多、中英文混合、空文件、单一字符和含四字节字符的文件。

## 测试

先按上面的命令编译主程序，再在 `code` 目录运行 Python 标准库测试：

```powershell
python tests/test_utf8.py
```

测试自动生成材料，并在仓库的 `tmp/utf8_huffman_tests` 下保存本次测试文件和 JSON 报告。内容包括：

- 40 组文件往返及逐字节比较。
- 与独立最小堆参考实现生成的完整压缩文件比较。
- 13 组非法 UTF-8 和 13 组损坏压缩文件的拒绝处理。
- 中文及含空格路径、交互菜单、已有文件保护。
- 四字节汉字、Emoji、BOM、空白、标点、组合字符及超过 256 种字符。

核心检查还覆盖全部合法 Unicode 标量的 UTF-8 转换及 64 位合并权值。Windows：

```powershell
g++ -std=c++17 -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8 tests/core_checks.cpp -o tests/core_checks.exe -lshell32
.\tests\core_checks.exe
```

Linux：

```bash
g++ -std=c++17 -O2 tests/core_checks.cpp -o tests/core_checks
./tests/core_checks
```

本仓库提供测试源码，不提交编译产物和临时测试文件。
