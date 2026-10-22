/*
 * 基于哈夫曼编码的ASCII文件无损压缩与还原（C++17）
 * ASCII字符以1字节读取，freq[128]统计频率；保留所有控制字符。
 * 核心采用HTNode、HuffmanTree、HuffmanCode、Select等教材式结构与函数。
 * 文件格式：总字符数8字节＋种类数4字节＋n条[ASCII码值1字节＋频次4字节]。
 * 整数为小端序；编码位从高位到低位保存，最后一个字节低位补零。
 * Windows: g++ -std=c++17 -O2 huffman_ascii.cpp -o huffman_ascii.exe -lshell32
 */

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdint>
#include <climits>
#include <limits>
#include <new>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif
#endif

using namespace std;
namespace fs=std::filesystem;
#ifndef OK
#define OK 1
#endif
#ifndef ERROR
#define ERROR 0
#endif
typedef int Status;
const uint32_t MAX_CHAR_TYPES = 128;
static_assert(CHAR_BIT==8,"本实验要求一个字节为8位");

typedef struct
{
    unsigned char data;          //ASCII码值，范围0..127
    uint32_t freq;               //出现次数
} CFNode;

typedef struct
{
    uint64_t charCount;          //原始字符总数（ASCII字符数，等于字节数）
    int length;                 //实际出现的字符种类数
    CFNode *elem;               //elem[1..length]按ASCII码值升序保存
} FrequencyTable;

typedef struct
{
    uint64_t weight;
    int parent;
    int lchild,rchild;
    unsigned char data;          //叶结点保存ASCII码值
} HTNode,*HuffmanTree;

typedef char **HuffmanCode;      //HC[i]对应字符表elem[i]
typedef struct {unsigned char byte;int count;} BitWriter;
typedef struct {unsigned char byte;int remain;} BitReader;

Status ReportError(string &error, const string &message)
{
    error = message;
    return ERROR;
}

Status CheckFileNames(const fs::path &inputName, const fs::path &outputName,
                      string &error)
{
    if(inputName.empty() || outputName.empty())
        return ReportError(error, "文件名不能为空。");

    error_code ec;
    if(!fs::is_regular_file(inputName, ec) || ec)
        return ReportError(error, "输入文件不存在或不是普通文件。");

    fs::file_status state = fs::symlink_status(outputName, ec);
    if(ec && ec != errc::no_such_file_or_directory)
        return ReportError(error, "无法检查输出文件路径。");
    if(fs::exists(state))
        return ReportError(error, "输出文件已存在，请使用新的文件名。");
    return OK;
}

Status Select(HuffmanTree HT, int k, int &s1, int &s2)
{
    s1 = s2 = 0;
    for(int i = 1; i <= k; ++i)
    {
        if(HT[i].parent != 0)
            continue;
        if(s1 == 0 || HT[i].weight < HT[s1].weight)
        {
            s2 = s1;
            s1 = i;
        }
        else if(s2 == 0 || HT[i].weight < HT[s2].weight)
            s2 = i;
    }
    return (s1 != 0 && s2 != 0) ? OK : ERROR;
}


//---------- 固定频率数组与按码值升序排列的字符表 ----------
void DestroyFrequencyTable(FrequencyTable &FT)
{
    delete [] FT.elem;
    FT.elem = nullptr;
    FT.length = 0;
    FT.charCount = 0;
}

Status CountFrequency(const fs::path &fileName, FrequencyTable &FT, string &error)
{
    FT = {0, 0, nullptr};
    ifstream fin(fileName, ios::binary);
    if(!fin) return ReportError(error, "无法打开ASCII文本文件。");
    uint32_t freq[128] = {};
    char ch;
    while(fin.get(ch))
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if(c >= 128)
            return ReportError(error, "输入含非ASCII字节，字节偏移：" + to_string(FT.charCount));
        if(freq[c] == numeric_limits<uint32_t>::max())
            return ReportError(error, "单个字符出现次数超过4字节频次范围。");
        ++freq[c];
        ++FT.charCount;
    }
    if(fin.bad() || !fin.eof())
        return ReportError(error, "读取原文件失败。");
    for(int c = 0; c < 128; ++c)
        if(freq[c] != 0) ++FT.length;
    if(FT.length == 0) return OK;
    FT.elem = new(nothrow) CFNode[FT.length + 1]();
    if(FT.elem == nullptr)
        return ReportError(error, "字符表存储空间分配失败。");
    int i = 1;
    for(int c = 0; c < 128; ++c)
    {
        if(freq[c] == 0) continue;
        FT.elem[i].data = static_cast<unsigned char>(c);
        FT.elem[i].freq = freq[c];
        ++i;
    }
    return OK;
}

//字符表按ASCII码值升序排列，可用折半查找定位字符。
int LocateCharacter(const FrequencyTable &FT,uint32_t cp)
{
    int low=1,high=FT.length;
    while(low<=high)
    {
        int mid=low+(high-low)/2;
        if(FT.elem[mid].data==cp)return mid;
        if(FT.elem[mid].data<cp)low=mid+1;
        else high=mid-1;
    }
    return 0;
}

//---------- 哈夫曼树：叶结点与字符表下标一致 ----------
Status CreateHuffmanTree(HuffmanTree &HT,const FrequencyTable &FT,string &error)
{
    HT=nullptr;
    int n=FT.length;
    if(n==0)return OK;
    int m=2*n-1;
    HT=new(nothrow) HTNode[m+1]();
    if(HT==nullptr)return ReportError(error,"哈夫曼树存储空间分配失败。");
    for(int i=1;i<=n;++i)
    {
        HT[i].data=FT.elem[i].data;
        HT[i].weight=FT.elem[i].freq;
    }
    for(int i=n+1;i<=m;++i)
    {
        int s1,s2;
        if(Select(HT,i-1,s1,s2)==ERROR)
        {
            delete [] HT;HT=nullptr;
            return ReportError(error,"选择最小权值结点失败。");
        }
        HT[s1].parent=HT[s2].parent=i;
        HT[i].lchild=s1;
        HT[i].rchild=s2;
        HT[i].weight=HT[s1].weight+HT[s2].weight;
    }
    return OK;
}

void DestroyHuffmanCode(HuffmanCode &HC,int n)
{
    if(HC!=nullptr)
    {
        for(int i=1;i<=n;++i)delete [] HC[i];
        delete [] HC;HC=nullptr;
    }
}

Status HuffmanCoding(HuffmanTree HT,int n,HuffmanCode &HC,string &error)
{
    HC=nullptr;
    if(n==0)return OK;
    HC=new(nothrow) char *[n+1]();
    if(HC==nullptr)return ReportError(error,"编码表存储空间分配失败。");
    char *cd=new(nothrow) char[n+1];
    if(cd==nullptr)
    {
        DestroyHuffmanCode(HC,n);
        return ReportError(error,"编码工作区存储空间分配失败。");
    }
    cd[n]='\0';
    for(int i=1;i<=n;++i)
    {
        int start=n,c=i,f=HT[i].parent;
        if(n==1)cd[--start]='0';
        while(f!=0)
        {
            cd[--start]=(HT[f].lchild==c)?'0':'1';
            c=f;f=HT[f].parent;
        }
        HC[i]=new(nothrow) char[n-start+1];
        if(HC[i]==nullptr)
        {
            delete [] cd;DestroyHuffmanCode(HC,n);
            return ReportError(error,"字符编码存储空间分配失败。");
        }
        strcpy(HC[i],&cd[start]);
    }
    delete [] cd;
    return OK;
}

Status WriteIntegerLE(ostream &fout, uint64_t value, int bytes)
{
    for(int i = 0; i < bytes; ++i)
    {
        fout.put(static_cast<char>(value & 0xFFu));
        value >>= 8;
    }
    return fout ? OK : ERROR;
}

Status ReadIntegerLE(istream &fin, uint64_t &value, int bytes)
{
    value = 0;
    for(int i = 0; i < bytes; ++i)
    {
        char ch;
        if(!fin.get(ch))
            return ERROR;
        uint64_t b = static_cast<unsigned char>(ch);
        value |= b << (8 * i);
    }
    return OK;
}


//---------- 可变长度文件头：8字节总数＋4字节种类数＋n条记录 ----------
Status WriteHeader(ostream &fout,const FrequencyTable &FT,string &error)
{
    if(WriteIntegerLE(fout,FT.charCount,8)==ERROR
       || WriteIntegerLE(fout,static_cast<uint64_t>(FT.length),4)==ERROR)
        return ReportError(error,"写入字符总数或种类数失败。");
    for(int i=1;i<=FT.length;++i)
        if(WriteIntegerLE(fout,FT.elem[i].data,1)==ERROR
           || WriteIntegerLE(fout,FT.elem[i].freq,4)==ERROR)
            return ReportError(error,"写入字符频率表失败。");
    return OK;
}

Status ReadHeader(istream &fin,FrequencyTable &FT,string &error)
{
    FT={0,0,nullptr};
    uint64_t types;
    if(ReadIntegerLE(fin,FT.charCount,8)==ERROR || ReadIntegerLE(fin,types,4)==ERROR)
        return ReportError(error,"文件头不完整。");
    if(types>MAX_CHAR_TYPES)
        return ReportError(error,"字符种类数超过ASCII范围。");
    FT.length=static_cast<int>(types);
    if(FT.length>0)
    {
        FT.elem=new(nothrow) CFNode[FT.length+1]();
        if(FT.elem==nullptr)return ReportError(error,"字符频率表存储空间分配失败。");
    }
    uint64_t sum=0;
    for(int i=1;i<=FT.length;++i)
    {
        uint64_t cp,freq;
        if(ReadIntegerLE(fin,cp,1)==ERROR || ReadIntegerLE(fin,freq,4)==ERROR)
        {
            DestroyFrequencyTable(FT);
            return ReportError(error,"字符频率表记录不完整。");
        }
        if(cp >= 128 || freq==0
           || (i>1 && cp<=FT.elem[i-1].data))
        {
            DestroyFrequencyTable(FT);
            return ReportError(error,"字符表包含非ASCII码值、零频率或重复/无序记录。");
        }
        FT.elem[i].data=static_cast<unsigned char>(cp);
        FT.elem[i].freq=static_cast<uint32_t>(freq);
        sum+=freq;
    }
    if(sum!=FT.charCount)
    {
        DestroyFrequencyTable(FT);
        return ReportError(error,"频率之和与原始字符总数不一致。");
    }
    return OK;
}

Status WriteBit(ostream &fout, BitWriter &B, int bit)
{
    B.byte = static_cast<unsigned char>((static_cast<unsigned int>(B.byte) << 1)
                                       | static_cast<unsigned int>(bit));
    ++B.count;
    if(B.count == 8)
    {
        fout.put(static_cast<char>(B.byte));
        B.byte = 0;
        B.count = 0;
    }
    return fout ? OK : ERROR;
}

Status FlushBits(ostream &fout, BitWriter &B)
{
    if(B.count > 0)
    {
        B.byte = static_cast<unsigned char>(static_cast<unsigned int>(B.byte)
                                           << (8 - B.count));
        fout.put(static_cast<char>(B.byte));
        B.byte = 0;
        B.count = 0;
    }
    return fout ? OK : ERROR;
}

Status ReadBit(istream &fin, BitReader &B, int &bit)
{
    if(B.remain == 0)
    {
        char ch;
        if(!fin.get(ch))
            return ERROR;
        B.byte = static_cast<unsigned char>(ch);
        B.remain = 8;
    }
    --B.remain;
    bit = (B.byte >> B.remain) & 1;
    return OK;
}

Status FinishOutput(ofstream &fout, const fs::path &fileName, Status result,
                     string &error)
{
    if(result == OK)
    {
        fout.flush();
        if(!fout)
            result = ReportError(error, "写入输出文件失败。");
    }
    fout.close();
    if(result == OK && !fout)
        result = ReportError(error, "关闭输出文件失败。");
    if(result == ERROR)
    {
        error_code ec;
        fs::remove(fileName, ec);
    }
    return result;
}

Status CompareFiles(const fs::path &firstName, const fs::path &secondName,
                      bool &same, string &error)
{
    same = false;
    ifstream first(firstName, ios::binary), second(secondName, ios::binary);
    if(!first || !second)
        return ReportError(error, "无法打开待比较的文件。");
    char a, b;
    while(true)
    {
        bool haveA = static_cast<bool>(first.get(a));
        bool haveB = static_cast<bool>(second.get(b));
        if(first.bad() || second.bad()
           || (!haveA && !first.eof()) || (!haveB && !second.eof()))
            return ReportError(error, "读取待比较文件失败。");
        if(haveA != haveB)
            return OK;
        if(!haveA)
        {
            same = true;
            return OK;
        }
        if(a != b)
            return OK;
    }
}


//---------- 按字符查编码表，按位打包 ----------
Status EncodeData(const fs::path &fileName, ostream &fout, const FrequencyTable &FT,
                  HuffmanCode HC, string &error)
{
    ifstream fin(fileName, ios::binary);
    if(!fin) return ReportError(error, "无法再次打开原文件。");
    int index[128] = {};
    uint32_t remaining[128] = {};
    for(int i = 1; i <= FT.length; ++i)
    {
        index[FT.elem[i].data] = i;
        remaining[FT.elem[i].data] = FT.elem[i].freq;
    }
    BitWriter B = {0, 0};
    char ch;
    while(fin.get(ch))
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if(c >= 128 || remaining[c] == 0)
            return ReportError(error, "原文件在统计后发生变化，请重新压缩。");
        --remaining[c];
        int i = index[c];
        for(const char *p = HC[i]; *p != '\0'; ++p)
            if(WriteBit(fout, B, *p - '0') == ERROR)
                return ReportError(error, "写入编码数据失败。");
    }
    if(fin.bad() || !fin.eof()) return ReportError(error, "再次读取原文件失败。");
    for(int c = 0; c < 128; ++c)
        if(remaining[c] != 0)
            return ReportError(error, "原文件在统计后发生变化，请重新压缩。");
    if(FlushBits(fout, B) == ERROR)
        return ReportError(error, "写入末尾编码失败。");
    return OK;
}

//---------- 编码指标：WPL按树深度计算，平均码长按实际编码计算 ----------
uint64_t CalculateWPL(HuffmanTree HT, int n)
{
    uint64_t wpl = 0;
    for(int i = 1; i <= n; ++i)
    {
        uint64_t depth = 0;
        for(int p = i; HT[p].parent != 0; p = HT[p].parent) ++depth;
        wpl += HT[i].weight * depth;
    }
    return wpl;
}

uint64_t EncodedBitCount(const FrequencyTable &FT, HuffmanCode HC)
{
    uint64_t bits = 0;
    for(int i = 1; i <= FT.length; ++i)
        bits += static_cast<uint64_t>(FT.elem[i].freq) * strlen(HC[i]);
    return bits;
}

double AverageLength(const FrequencyTable &FT, HuffmanCode HC)
{
    if(FT.charCount == 0) return 0.0; //空文件由显示函数标记为“不计算”
    return static_cast<double>(EncodedBitCount(FT, HC)) / FT.charCount;
}

double InformationEntropy(const FrequencyTable &FT)
{
    double h = 0.0;
    for(int i = 1; i <= FT.length; ++i)
    {
        double p = static_cast<double>(FT.elem[i].freq) / FT.charCount;
        h -= p * log2(p);
    }
    return h;
}

void PrintMetrics(const FrequencyTable &FT, HuffmanTree HT, HuffmanCode HC)
{
    uint64_t bits = EncodedBitCount(FT, HC);
    uint64_t header = 12 + 5 * static_cast<uint64_t>(FT.length);
    cout << "WPL：" << CalculateWPL(HT, FT.length) << "\n";
    if(FT.charCount == 0)
        cout << "平均码长：—\n信息熵：—\n";
    else
        cout << fixed << setprecision(6)
             << "平均码长：" << AverageLength(FT, HC) << " bit/字符\n"
             << "信息熵：" << InformationEntropy(FT) << " bit/字符\n";
    cout << "编码数据总位数：" << bits
         << "\n预计压缩文件字节数：" << header + (bits + 7) / 8 << "\n";
}

Status CompressFile(const fs::path &inputName,const fs::path &outputName,
                    uint64_t &originalBytes,uint64_t &originalCharacters,string &error)
{
    if(CheckFileNames(inputName,outputName,error)==ERROR)return ERROR;
    FrequencyTable FT={0,0,nullptr};
    if(CountFrequency(inputName,FT,error)==ERROR)return ERROR;
    originalBytes=FT.charCount;
    originalCharacters=FT.charCount;
    HuffmanTree HT=nullptr;HuffmanCode HC=nullptr;
    Status result=CreateHuffmanTree(HT,FT,error);
    if(result==OK)result=HuffmanCoding(HT,FT.length,HC,error);
    if(result==OK)result=CheckFileNames(inputName,outputName,error);
    if(result==OK)
    {
        ofstream fout(outputName,ios::binary);
        if(!fout)result=ReportError(error,"无法创建压缩文件。");
        else
        {
            result=WriteHeader(fout,FT,error);
            if(result==OK)result=EncodeData(inputName,fout,FT,HC,error);
            result=FinishOutput(fout,outputName,result,error);
        }
    }
    if(result==OK)PrintMetrics(FT,HT,HC);
    DestroyHuffmanCode(HC,FT.length);
    delete [] HT;DestroyFrequencyTable(FT);
    return result;
}

Status DecodeData(istream &fin,ostream &fout,const FrequencyTable &FT,
                  HuffmanTree HT,uint32_t remaining[],string &error)
{
    int n=FT.length,root=(n==0)?0:2*n-1,p=root;
    for(int i=1;i<=n;++i)remaining[i]=FT.elem[i].freq;
    BitReader B={0,0};
    uint64_t count=0;
    while(count<FT.charCount)
    {
        int bit;
        if(ReadBit(fin,B,bit)==ERROR)
            return ReportError(error,"编码数据不完整，无法还原全部字符。");
        if(n==1)
        {
            if(bit!=0)return ReportError(error,"单一字符的编码应为0。");
            p=root;
        }
        else
        {
            p=(bit==0)?HT[p].lchild:HT[p].rchild;
            if(p==0)return ReportError(error,"编码路径无效。");
        }
        if(HT[p].lchild==0 && HT[p].rchild==0)
        {
            if(remaining[p]==0)return ReportError(error,"编码数据与字符频率表不一致。");
            --remaining[p];
            fout.put(static_cast<char>(HT[p].data));
            if(!fout)return ReportError(error,"写入还原文本失败。");
            ++count;p=root;
        }
    }
    if(B.remain>0)
    {
        unsigned int mask=(1u<<B.remain)-1u;
        if((static_cast<unsigned int>(B.byte) & mask)!=0)
            return ReportError(error,"末尾补零位不符合约定。");
    }
    char extra;
    if(fin.get(extra))return ReportError(error,"压缩文件包含多余的数据字节。");
    if(fin.bad() || !fin.eof())return ReportError(error,"读取压缩文件失败。");
    return OK;
}

Status DecompressFile(const fs::path &inputName,const fs::path &outputName,string &error)
{
    if(CheckFileNames(inputName,outputName,error)==ERROR)return ERROR;
    ifstream fin(inputName,ios::binary);
    if(!fin)return ReportError(error,"无法打开压缩文件。");
    FrequencyTable FT={0,0,nullptr};
    if(ReadHeader(fin,FT,error)==ERROR)return ERROR;
    HuffmanTree HT=nullptr;
    Status result=CreateHuffmanTree(HT,FT,error);
    uint32_t *remaining=nullptr;
    if(result==OK)
    {
        remaining=new(nothrow) uint32_t[FT.length+1];
        if(remaining==nullptr)result=ReportError(error,"译码工作区存储空间分配失败。");
    }
    if(result==OK)
    {
        ofstream fout(outputName,ios::binary);
        if(!fout)result=ReportError(error,"无法创建还原文本文件。");
        else
        {
            result=DecodeData(fin,fout,FT,HT,remaining,error);
            result=FinishOutput(fout,outputName,result,error);
        }
    }
    delete [] remaining;delete [] HT;DestroyFrequencyTable(FT);
    return result;
}

//---------- 字符显示：空白与控制字符使用可见标签 ----------
string CharacterLabel(uint32_t c)
{
    switch(c)
    {
        case 0: return "[NUL]";
        case 9: return "[TAB]";
        case 10: return "[LF]";
        case 13: return "[CR]";
        case 32: return "[空格]";
        case 127: return "[DEL]";
    }
    if(c < 32) return "[CTRL-" + to_string(c) + "]";
    return string(1, static_cast<char>(c));
}

Status ShowHuffmanCode(const fs::path &fileName, string &error)
{
    FrequencyTable FT = {0, 0, nullptr};
    if(CountFrequency(fileName, FT, error) == ERROR) return ERROR;
    HuffmanTree HT = nullptr;
    HuffmanCode HC = nullptr;
    Status result = CreateHuffmanTree(HT, FT, error);
    if(result == OK) result = HuffmanCoding(HT, FT.length, HC, error);
    if(result == OK)
    {
        cout << "原始字节数：" << FT.charCount << "\n字符总数：" << FT.charCount
             << "\n字符种类数：" << FT.length << "\n";
        cout << "ASCII码值  字符  出现次数  编码长度  哈夫曼编码\n";
        for(int i = 1; i <= FT.length; ++i)
            cout << static_cast<unsigned int>(FT.elem[i].data) << "  "
                 << CharacterLabel(FT.elem[i].data) << "  " << FT.elem[i].freq
                 << "  " << strlen(HC[i]) << "  " << HC[i] << "\n";
        PrintMetrics(FT, HT, HC);
    }
    DestroyHuffmanCode(HC, FT.length);
    delete [] HT;
    DestroyFrequencyTable(FT);
    return result;
}

void PrintUsage()
{
    cout<<"用法：\n"
        <<"  huffman_ascii c ASCII文本 压缩文件\n"
        <<"  huffman_ascii d 压缩文件 还原文本\n"
        <<"  huffman_ascii v 原始文本 还原文本\n"
        <<"  huffman_ascii t ASCII文本\n"
        <<"不带参数进入菜单。输出文件使用新的文件名。\n";
}

int RunCommand(const string &operation,const fs::path &firstName,const fs::path &secondName)
{
    string error;Status result=ERROR;
    uint64_t originalBytes=0,originalCharacters=0;
    bool same=false;
    auto start=chrono::steady_clock::now();
    if(operation=="c")result=CompressFile(firstName,secondName,originalBytes,originalCharacters,error);
    else if(operation=="d")result=DecompressFile(firstName,secondName,error);
    else if(operation=="v")result=CompareFiles(firstName,secondName,same,error);
    else if(operation=="t")result=ShowHuffmanCode(firstName,error);
    else {PrintUsage();return 2;}
    if(result==ERROR){cerr<<"操作失败："<<error<<"\n";return 1;}
    if(operation=="c")
    {
        error_code ec;uintmax_t size=fs::file_size(secondName,ec);
        cout<<"压缩完成。\n原始字节数："<<originalBytes
            <<"\n原始字符数："<<originalCharacters<<"\n";
        if(!ec)
        {
            cout<<"压缩文件字节数："<<size<<"\n";
            if(originalBytes==0)cout<<"压缩率：空文件不计算。\n";
            else cout<<fixed<<setprecision(2)<<"压缩率："
                     <<static_cast<double>(size)/static_cast<double>(originalBytes)*100.0<<"%\n";
        }
    }
    else if(operation=="d")cout<<"解压完成。\n";
    else if(operation=="v")
    {
        cout<<(same?"验证通过：两个文件的长度和字节内容完全一致。\n"
                   :"验证未通过：两个文件不一致。\n");
        return same?0:1;
    }
    if(operation=="c" || operation=="d")
    {
        double ms=chrono::duration<double,milli>(chrono::steady_clock::now()-start).count();
        cout<<fixed<<setprecision(3)<<"处理耗时："<<ms<<" ms\n";
    }
    return 0;
}

bool ReadFileName(const string &prompt, fs::path &fileName)
{
    cout << prompt;
    string text;
    if(!getline(cin, text))
        return false;
    if(text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    fileName = fs::u8path(text);
    return true;
}

fs::path ArgumentPath(int index, char *argv[])
{
#ifdef _WIN32
    (void)argv;
    int count;
    LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &count);
    if(args == nullptr)
        throw runtime_error("读取Windows命令行失败。");
    if(index >= count)
    {
        LocalFree(args);
        throw runtime_error("命令行参数不足。");
    }
    fs::path result(args[index]);
    LocalFree(args);
    return result;
#else
    return fs::path(argv[index]);
#endif
}

void RunMenu()
{
    while(true)
    {
        cout << "\n===== ASCII字符哈夫曼压缩实验 =====\n"
             << "1. 压缩文件\n2. 解压文件\n3. 比较两个文件\n"
             << "4. 查看频率及编码表\n0. 退出\n请选择：";
        string choice;
        if(!getline(cin, choice) || choice == "0")
            return;
        if(choice != "1" && choice != "2" && choice != "3" && choice != "4")
        {
            cout << "请输入0至4之间的选项。\n";
            continue;
        }
        fs::path firstName, secondName;
        if(!ReadFileName("请输入输入文件名：", firstName))
            return;
        if(choice != "4" && !ReadFileName("请输入第二个文件名：", secondName))
            return;
        string operation = (choice == "1") ? "c" : (choice == "2") ? "d"
                           : (choice == "3") ? "v" : "t";
        RunCommand(operation, firstName, secondName);
    }
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    try
    {
        if(argc == 1)
        {
            RunMenu();
            return 0;
        }
        string operation = argv[1];
        if(operation == "--help" || operation == "-h")
        {
            PrintUsage();
            return 0;
        }
        if((operation == "t" && argc != 3)
           || ((operation == "c" || operation == "d" || operation == "v")
               && argc != 4)
           || (operation != "c" && operation != "d" && operation != "v"
               && operation != "t"))
        {
            PrintUsage();
            return 2;
        }
        fs::path firstName = ArgumentPath(2, argv);
        fs::path secondName;
        if(argc == 4)
            secondName = ArgumentPath(3, argv);
        return RunCommand(operation, firstName, secondName);
    }
    catch(const exception &e)
    {
        cerr << "程序异常：" << e.what() << "\n";
        return 1;
    }
}

