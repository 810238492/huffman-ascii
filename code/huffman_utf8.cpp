/*
 * 基于字符的UTF-8文本哈夫曼压缩实验（C++17）
 * 以Unicode码点为编码符号，保留空格、标点、CR/LF、TAB和BOM。
 * 核心采用HTNode、HuffmanTree、HuffmanCode、Select等教材式结构与函数。
 *
 * 文件格式（全部整数为小端序）：
 *   原始字符数：8字节；字符种类数m：4字节；
 *   m条记录：码点4字节＋出现次数4字节，按码点升序保存；
 *   数据区从偏移12+8*m开始，编码位按高位到低位读取，末尾低位补零。
 * Windows：g++ -std=c++17 -O2 huffman_utf8.cpp -o huffman_utf8.exe -lshell32
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
#include <map>

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
const uint32_t MAX_CHAR_TYPES=0x110000u-0x800u;
static_assert(CHAR_BIT==8,"本实验要求一个字节为8位");

typedef struct
{
    uint32_t data;               //字符的Unicode码点
    uint32_t freq;               //出现次数
} CFNode;

typedef struct
{
    uint64_t charCount;          //原始字符总数（Unicode码点数）
    int length;                 //实际出现的字符种类数
    CFNode *elem;               //elem[1..length]按码点升序保存
} FrequencyTable;

typedef struct
{
    uint64_t weight;
    int parent;
    int lchild,rchild;
    uint32_t data;               //叶结点保存完整字符的码点
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


//---------- UTF-8解析与还原 ----------
bool IsUnicodeScalar(uint32_t cp)
{
    return cp<=0x10FFFFu && !(cp>=0xD800u && cp<=0xDFFFu);
}

Status DecodeUTF8(const string &raw,u32string &text,string &error)
{
    text.clear();
    for(size_t i=0;i<raw.size();)
    {
        unsigned char first=static_cast<unsigned char>(raw[i]);
        uint32_t cp,minimum;
        size_t bytes;
        if(first<=0x7Fu)         {cp=first;minimum=0;bytes=1;}
        else if(first>=0xC2u && first<=0xDFu)
                                {cp=first & 0x1Fu;minimum=0x80u;bytes=2;}
        else if(first>=0xE0u && first<=0xEFu)
                                {cp=first & 0x0Fu;minimum=0x800u;bytes=3;}
        else if(first>=0xF0u && first<=0xF4u)
                                {cp=first & 0x07u;minimum=0x10000u;bytes=4;}
        else
            return ReportError(error,"UTF-8首字节无效，字节偏移："+to_string(i));
        if(raw.size()-i<bytes)
            return ReportError(error,"UTF-8字符不完整，字节偏移："+to_string(i));
        for(size_t j=1;j<bytes;++j)
        {
            unsigned char b=static_cast<unsigned char>(raw[i+j]);
            if((b & 0xC0u)!=0x80u)
                return ReportError(error,"UTF-8后续字节无效，字节偏移："+to_string(i+j));
            cp=(cp<<6) | (b & 0x3Fu);
        }
        if(cp<minimum || !IsUnicodeScalar(cp))
            return ReportError(error,"UTF-8包含非最短编码或无效码点，字节偏移："+to_string(i));
        text.push_back(static_cast<char32_t>(cp));
        i+=bytes;
    }
    return OK;
}

//将一个合法码点转换为UTF-8字节，返回字节数。
int EncodeUTF8(uint32_t cp,char bytes[4])
{
    if(!IsUnicodeScalar(cp))return 0;
    if(cp<=0x7Fu)
    {
        bytes[0]=static_cast<char>(cp);
        return 1;
    }
    if(cp<=0x7FFu)
    {
        bytes[0]=static_cast<char>(0xC0u | (cp>>6));
        bytes[1]=static_cast<char>(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if(cp<=0xFFFFu)
    {
        bytes[0]=static_cast<char>(0xE0u | (cp>>12));
        bytes[1]=static_cast<char>(0x80u | ((cp>>6) & 0x3Fu));
        bytes[2]=static_cast<char>(0x80u | (cp & 0x3Fu));
        return 3;
    }
    bytes[0]=static_cast<char>(0xF0u | (cp>>18));
    bytes[1]=static_cast<char>(0x80u | ((cp>>12) & 0x3Fu));
    bytes[2]=static_cast<char>(0x80u | ((cp>>6) & 0x3Fu));
    bytes[3]=static_cast<char>(0x80u | (cp & 0x3Fu));
    return 4;
}

Status ReadUTF8Text(const fs::path &fileName,string &raw,u32string &text,string &error)
{
    ifstream fin(fileName,ios::binary);
    if(!fin)return ReportError(error,"无法打开UTF-8文本文件。");
    raw.clear();
    char block[8192];
    while(fin.read(block,sizeof(block)) || fin.gcount()>0)
        raw.append(block,static_cast<size_t>(fin.gcount()));
    if(fin.bad() || !fin.eof())
        return ReportError(error,"读取文本文件失败。");
    return DecodeUTF8(raw,text,error);
}

//---------- 字符频率表 ----------
void DestroyFrequencyTable(FrequencyTable &FT)
{
    delete [] FT.elem;
    FT.elem=nullptr;
    FT.length=0;
    FT.charCount=0;
}

Status CountFrequency(const u32string &text,FrequencyTable &FT,string &error)
{
    FT={0,0,nullptr};
    map<uint32_t,uint32_t> counts;
    for(char32_t ch:text)
    {
        uint32_t cp=static_cast<uint32_t>(ch);
        uint32_t &freq=counts[cp];
        if(freq==numeric_limits<uint32_t>::max())
            return ReportError(error,"单个字符出现次数超过4字节频率项的范围。");
        ++freq;
    }
    FT.charCount=static_cast<uint64_t>(text.size());
    FT.length=static_cast<int>(counts.size());
    if(FT.length==0)return OK;
    FT.elem=new(nothrow) CFNode[FT.length+1]();
    if(FT.elem==nullptr)
        return ReportError(error,"字符频率表存储空间分配失败。");
    int i=1;
    for(const auto &item:counts)
    {
        FT.elem[i].data=item.first;
        FT.elem[i].freq=item.second;
        ++i;
    }
    return OK;
}

//字符表按码点升序排列，编码时用折半查找定位字符。
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
        if(WriteIntegerLE(fout,FT.elem[i].data,4)==ERROR
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
        return ReportError(error,"字符种类数超出Unicode码点范围。");
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
        if(ReadIntegerLE(fin,cp,4)==ERROR || ReadIntegerLE(fin,freq,4)==ERROR)
        {
            DestroyFrequencyTable(FT);
            return ReportError(error,"字符频率表记录不完整。");
        }
        if(!IsUnicodeScalar(static_cast<uint32_t>(cp)) || freq==0
           || (i>1 && cp<=FT.elem[i-1].data))
        {
            DestroyFrequencyTable(FT);
            return ReportError(error,"字符表包含无效码点、零频率或重复/无序记录。");
        }
        FT.elem[i].data=static_cast<uint32_t>(cp);
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
Status EncodeData(const u32string &text,ostream &fout,const FrequencyTable &FT,
                  HuffmanCode HC,string &error)
{
    BitWriter B={0,0};
    for(char32_t ch:text)
    {
        int i=LocateCharacter(FT,static_cast<uint32_t>(ch));
        if(i==0 || HC==nullptr || HC[i]==nullptr)
            return ReportError(error,"字符未能在编码表中定位。");
        for(const char *p=HC[i];*p!='\0';++p)
            if(WriteBit(fout,B,*p-'0')==ERROR)
                return ReportError(error,"写入编码数据失败。");
    }
    if(FlushBits(fout,B)==ERROR)return ReportError(error,"写入末尾编码数据失败。");
    return OK;
}

Status CompressFile(const fs::path &inputName,const fs::path &outputName,
                    uint64_t &originalBytes,uint64_t &originalCharacters,string &error)
{
    if(CheckFileNames(inputName,outputName,error)==ERROR)return ERROR;
    string raw;u32string text;
    if(ReadUTF8Text(inputName,raw,text,error)==ERROR)return ERROR;
    originalBytes=static_cast<uint64_t>(raw.size());
    originalCharacters=static_cast<uint64_t>(text.size());
    FrequencyTable FT={0,0,nullptr};
    if(CountFrequency(text,FT,error)==ERROR)return ERROR;
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
            if(result==OK)result=EncodeData(text,fout,FT,HC,error);
            result=FinishOutput(fout,outputName,result,error);
        }
    }
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
            char bytes[4];
            int length=EncodeUTF8(HT[p].data,bytes);
            if(length==0)return ReportError(error,"还原字符码点无效。");
            fout.write(bytes,length);
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
string CharacterLabel(uint32_t cp)
{
    switch(cp)
    {
        case 0:return "[NUL]";
        case 9:return "[TAB]";
        case 10:return "[LF]";
        case 13:return "[CR]";
        case 32:return "[空格]";
        case 0xA0:return "[不换行空格]";
        case 0x3000:return "[全角空格]";
        case 0xFEFF:return "[BOM]";
        case 0x2028:return "[LS]";
        case 0x2029:return "[PS]";
        case 0x200B:return "[零宽空格]";
        case 0x200C:return "[ZWNJ]";
        case 0x200D:return "[ZWJ]";
        case 0x2060:return "[WJ]";
    }
    if(cp<0x20u || (cp>=0x7Fu && cp<=0x9Fu))return "[控制字符]";
    if(cp==0x1680u || (cp>=0x2000u && cp<=0x200Au)
       || cp==0x202Fu || cp==0x205Fu)return "[空白字符]";
    if((cp>=0x202Au && cp<=0x202Eu) || (cp>=0x2066u && cp<=0x2069u))return "[格式字符]";
    char bytes[4];int length=EncodeUTF8(cp,bytes);
    return string(bytes,static_cast<size_t>(length));
}

Status ShowHuffmanCode(const fs::path &fileName,string &error)
{
    string raw;u32string text;
    if(ReadUTF8Text(fileName,raw,text,error)==ERROR)return ERROR;
    FrequencyTable FT={0,0,nullptr};
    if(CountFrequency(text,FT,error)==ERROR)return ERROR;
    HuffmanTree HT=nullptr;HuffmanCode HC=nullptr;
    Status result=CreateHuffmanTree(HT,FT,error);
    if(result==OK)result=HuffmanCoding(HT,FT.length,HC,error);
    if(result==OK)
    {
        cout<<"原始字节数："<<raw.size()<<"\n字符总数："<<FT.charCount
            <<"\n字符种类数："<<FT.length<<"\n";
        cout<<"Unicode码点  字符  出现次数  编码长度  哈夫曼编码\n";
        uint64_t bits=0;
        for(int i=1;i<=FT.length;++i)
        {
            size_t length=strlen(HC[i]);
            cout<<"U+"<<uppercase<<hex<<setw(4)<<setfill('0')<<FT.elem[i].data
                <<dec<<nouppercase<<setfill(' ')<<"  "<<CharacterLabel(FT.elem[i].data)
                <<"  "<<FT.elem[i].freq<<"  "<<length<<"  "<<HC[i]<<"\n";
            bits+=static_cast<uint64_t>(FT.elem[i].freq)*length;
        }
        uint64_t header=12+8*static_cast<uint64_t>(FT.length);
        cout<<"编码数据总位数："<<bits<<"\n预计压缩文件字节数："
            <<header+(bits+7)/8<<"\n";
    }
    DestroyHuffmanCode(HC,FT.length);delete [] HT;DestroyFrequencyTable(FT);
    return result;
}

void PrintUsage()
{
    cout<<"用法：\n"
        <<"  huffman_utf8 c UTF-8文本 压缩文件\n"
        <<"  huffman_utf8 d 压缩文件 还原文本\n"
        <<"  huffman_utf8 v 原始文本 还原文本\n"
        <<"  huffman_utf8 t UTF-8文本\n"
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
        cout << "\n===== UTF-8字符哈夫曼压缩实验 =====\n"
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

