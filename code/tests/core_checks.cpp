#define main HuffmanASCIIDemoMain
#include "../huffman_ascii.cpp"
#undef main
#include <cassert>
#include <sstream>

int main()
{
    string error;
    FrequencyTable FT={21,4,new CFNode[5]()};
    const uint32_t freq[4]={3,3,6,9};
    for(int i=1;i<=4;++i) {FT.elem[i].data=static_cast<unsigned char>('A'+i-1);FT.elem[i].freq=freq[i-1];}
    HuffmanTree HT=nullptr;HuffmanCode HC=nullptr;
    assert(CreateHuffmanTree(HT,FT,error)==OK);
    assert(HuffmanCoding(HT,4,HC,error)==OK);
    assert(string(HC[1])=="110" && string(HC[2])=="111");
    assert(string(HC[3])=="10" && string(HC[4])=="0");
    assert(CalculateWPL(HT,4)==39);
    assert(abs(AverageLength(FT,HC)-39.0/21)<1e-12);
    assert(abs(InformationEntropy(FT)-1.8423709931771084)<1e-12);
    stringstream data(ios::in|ios::out|ios::binary);
    assert(WriteHeader(data,FT,error)==OK && data.str().size()==32);
    FrequencyTable read={0,0,nullptr};
    assert(ReadHeader(data,read,error)==OK && read.charCount==21 && read.length==4);
    DestroyFrequencyTable(read);DestroyHuffmanCode(HC,4);delete [] HT;DestroyFrequencyTable(FT);

    FT={1,1,new CFNode[2]()};FT.elem[1]={'A',1};
    assert(CreateHuffmanTree(HT,FT,error)==OK && HuffmanCoding(HT,1,HC,error)==OK);
    assert(CalculateWPL(HT,1)==0 && string(HC[1])=="0");
    assert(AverageLength(FT,HC)==1 && InformationEntropy(FT)==0);
    DestroyHuffmanCode(HC,1);delete [] HT;DestroyFrequencyTable(FT);

    FT={static_cast<uint64_t>(UINT32_MAX)*128,128,new CFNode[129]()};
    for(int i=1;i<=128;++i) {FT.elem[i].data=static_cast<unsigned char>(i-1);FT.elem[i].freq=UINT32_MAX;}
    assert(CreateHuffmanTree(HT,FT,error)==OK && HT[255].weight==FT.charCount);
    assert(CalculateWPL(HT,128)==FT.charCount*7);
    delete [] HT;DestroyFrequencyTable(FT);
    cout<<"ASCII tree, tie rule, header, metrics and 64-bit weight checks passed\n";
}
