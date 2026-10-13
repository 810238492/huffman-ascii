#define main HuffmanUTF8DemoMain
#include "../huffman_utf8.cpp"
#undef main
#include <cassert>

int main()
{
    string error,raw;u32string text;
    assert(DecodeUTF8(u8"中中中AB",text,error)==OK && text.size()==5);
    FrequencyTable FT={0,0,nullptr};
    assert(CountFrequency(text,FT,error)==OK && FT.length==3);
    assert(FT.elem[3].data==0x4E2Du && FT.elem[3].freq==3);
    HuffmanTree HT=nullptr;HuffmanCode HC=nullptr;
    assert(CreateHuffmanTree(HT,FT,error)==OK);
    assert(HuffmanCoding(HT,FT.length,HC,error)==OK);
    assert(string(HC[1])=="00" && string(HC[2])=="01" && string(HC[3])=="1");
    DestroyHuffmanCode(HC,FT.length);delete [] HT;DestroyFrequencyTable(FT);

    //全部合法Unicode标量的UTF-8转换边界，跳过代理码点。
    for(uint32_t cp=0;cp<=0x10FFFFu;++cp)
    {
        if(!IsUnicodeScalar(cp))continue;
        char bytes[4];int n=EncodeUTF8(cp,bytes);
        raw.append(bytes,static_cast<size_t>(n));
    }
    assert(DecodeUTF8(raw,text,error)==OK && text.size()==MAX_CHAR_TYPES);
    size_t i=0;
    for(uint32_t cp=0;cp<=0x10FFFFu;++cp)
        if(IsUnicodeScalar(cp))assert(static_cast<uint32_t>(text[i++])==cp);

    //600种字符，合并权值必须使用64位。
    FT.charCount=static_cast<uint64_t>(numeric_limits<uint32_t>::max())*600;
    FT.length=600;
    FT.elem=new CFNode[601]();
    for(int j=1;j<=600;++j)
    {
        FT.elem[j].data=0x4E00u+static_cast<uint32_t>(j);
        FT.elem[j].freq=numeric_limits<uint32_t>::max();
    }
    assert(CreateHuffmanTree(HT,FT,error)==OK);
    assert(HT[1199].weight==FT.charCount);
    assert(LocateCharacter(FT,0x4E01u)==1 && LocateCharacter(FT,0)==0);
    delete [] HT;DestroyFrequencyTable(FT);
    cout<<"Unicode scalar and tree checks passed\n";
    return 0;
}
