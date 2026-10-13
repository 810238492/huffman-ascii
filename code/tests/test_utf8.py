"""独立最小堆参考编码、UTF-8边界和文件往返验证。Python标准库。"""
from pathlib import Path
from collections import Counter
import heapq
import json
import os
import random
import struct
import subprocess
import uuid

CODE=Path(__file__).resolve().parents[1]
ROOT=CODE.parent
EXE=CODE/('huffman_utf8.exe' if os.name=='nt' else 'huffman_utf8')
OUT=ROOT/'tmp'/'utf8_huffman_tests'/uuid.uuid4().hex[:10]
FILES=OUT/'中文 空格路径'
FILES.mkdir(parents=True)


def run(*args, input_text=None, expect=0):
    p=subprocess.run([str(EXE),*map(str,args)],cwd=CODE,
        input=input_text,capture_output=True,encoding='utf-8',timeout=60,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    assert p.returncode==expect,(args,p.returncode,p.stdout,p.stderr)
    return p


def codes_for(text):
    freq=Counter(text)
    heap=[]
    for i,ch in enumerate(sorted(freq),1):
        heapq.heappush(heap,(freq[ch],i,('leaf',ch)))
    if not heap:return {}
    if len(heap)==1:return {heap[0][2][1]:'0'}
    i=len(heap)+1
    while len(heap)>1:
        a,_,left=heapq.heappop(heap)
        b,_,right=heapq.heappop(heap)
        heapq.heappush(heap,(a+b,i,('branch',left,right)))
        i+=1
    codes={}

    def visit(node,code):
        if node[0]=='leaf':codes[node[1]]=code
        else:
            visit(node[1],code+'0')
            visit(node[2],code+'1')
    visit(heap[0][2],'')
    return codes


def reference_file(data):
    text=data.decode('utf-8')
    counts=Counter(text)
    header=struct.pack('<QI',len(text),len(counts))
    header+=b''.join(struct.pack('<II',ord(ch),counts[ch]) for ch in sorted(counts))
    codes=codes_for(text)
    bits=''.join(codes[ch] for ch in text)
    bits+='0'*(-len(bits)%8)
    payload=bytes(int(bits[i:i+8],2) for i in range(0,len(bits),8))
    return header+payload


rng=random.Random(20261008)
cases=[
 ('empty',''),('single_ascii','A'),('single_han','中'*9),
 ('single_supplementary_han','\U00020000'*9),('single_emoji','😀'*7),
 ('single_nul','\x00'*17),('single_space',' '*17),('demo','中中中AB'),
 ('punctuation','中 A，中！\r\n\t  。,!?\u3000'),
 ('ascii_all',''.join(chr(x) for x in range(128))),
 ('two_byte',''.join(chr(x) for x in range(0x80,0x200))),
 ('utf8_boundaries',''.join(chr(x) for x in [0,0x7F,0x80,0x7FF,0x800,0xD7FF,0xE000,0xFFFF,0x10000,0x10FFFF])),
 ('bom','\ufeff中文A\r\n\ufeffB'),('crlf','甲\r\n乙\n丙\r丁\t终'),
 ('combining','e\u0301é 中文'),('emoji_sequence','👨\u200d👩\u200d👧\u200d👦 😊'),
 ('ties','甲乙丙丁甲乙丙丁'),
 ('unicode_spaces','甲\u00a0\u1680\u2000\u200a\u2028\u2029\u202f\u205f\u3000乙\u200b\u200c\u200d'),
 ('more_than_256',''.join(chr(0x4E00+x) for x in range(600))*2),
 ('larger_text','哈夫曼编码实验：中英文、数字123和标点。\r\n\t  '*4000),
]
pool=[chr(x) for x in range(128)]+[chr(0x4E00+x) for x in range(180)]+[chr(0x20000+x) for x in range(40)]+[chr(0x80+x) for x in range(80)]
for i in range(20):
    alphabet=rng.sample(pool,rng.randrange(2,len(pool)))
    text=''.join(rng.choice(alphabet) for _ in range(rng.randrange(1,3501)))
    cases.append((f'random_{i:02d}',text))

report=[]
for name,text in cases:
    data=text.encode('utf-8')
    source=FILES/(name+' 原始.txt')
    compressed=FILES/(name+' 压缩.huf8')
    restored=FILES/(name+' 还原.txt')
    source.write_bytes(data)
    run('c',source,compressed)
    actual=compressed.read_bytes()
    assert actual==reference_file(data),name
    run('d',compressed,restored)
    assert restored.read_bytes()==data,name
    run('v',source,restored)
    report.append({'case':name,'characters':len(text),'types':len(set(text)),
                   'original_bytes':len(data),'compressed_bytes':len(actual)})

#汉字频率与码长可直接显示；空白、BOM、四字节字符同样能查看。
for name in ['demo','punctuation','bom','single_supplementary_han','emoji_sequence','utf8_boundaries','unicode_spaces']:
    text=dict(cases)[name]
    source=FILES/(name+' 原始.txt')
    shown=run('t',source).stdout
    rows=[line for line in shown.splitlines() if line.startswith('U+')]
    counts=Counter(text);codes=codes_for(text)
    assert len(rows)==len(counts),(name,shown)
    for row in rows:
        label,count,length,code=row.rsplit(None,3)
        cp=int(label.split()[0][2:],16)
        ch=chr(cp)
        assert int(count)==counts[ch] and int(length)==len(codes[ch]) and code==codes[ch]
    if name=='demo':
        assert codes=={'A':'00','B':'01','中':'1'}
        (OUT/'示例编码表.txt').write_text(shown,encoding='utf-8')

#输入必须是合法UTF-8，不用替换字符掩盖原始字节错误。
invalid_utf8={
 'continuation_only':b'\x80','overlong_two':b'\xc0\xaf',
 'overlong_three':b'\xe0\x80\x80','overlong_four':b'\xf0\x80\x80\x80',
 'surrogate':b'\xed\xa0\x80','too_large':b'\xf4\x90\x80\x80',
 'invalid_lead':b'\xff','bad_continuation':b'\xe4A\xad',
 'short_two':b'\xc2','short_three':b'\xe4\xb8','short_four':b'\xf0\x90\x80',
 'gbk_han':'中文'.encode('gbk'),'utf16_bom':b'\xff\xfeA\x00',
}
for name,data in invalid_utf8.items():
    source=FILES/('invalid_'+name+'.txt')
    target=FILES/('invalid_'+name+'.huf8')
    source.write_bytes(data)
    run('c',source,target,expect=1)
    assert not target.exists()

valid=(FILES/'demo 压缩.huf8').read_bytes()
header_size=12+8*3
assert valid[header_size:]==b'\xe2'
bad_files={
 'short_header':b'\x00'*8,
 'short_table':valid[:header_size-1],
 'short_data':valid[:header_size],
 'extra_data':valid+b'\x00',
 'bad_padding':valid[:-1]+bytes([valid[-1]|1]),
 'bad_total':struct.pack('<Q',6)+valid[8:],
 'too_many_types':struct.pack('<QI',0,0x110000),
 'invalid_scalar':valid[:12]+struct.pack('<I',0xD800)+valid[16:],
 'duplicate_scalar':valid[:20]+valid[12:16]+valid[24:],
 'zero_frequency':valid[:16]+b'\x00'*4+valid[20:],
 'wrong_histogram':reference_file(b'AB')[:-1]+b'\x00',
 'single_bad_bit':reference_file('中'.encode())[:-1]+b'\x80',
 'empty_extra':reference_file(b'')+b'\x00',
}
for name,data in bad_files.items():
    source=FILES/('bad_'+name+'.huf8')
    output=FILES/('bad_'+name+'.txt')
    source.write_bytes(data)
    run('d',source,output,expect=1)
    assert not output.exists(),name

existing=FILES/'existing.txt'
existing.write_bytes(b'keep existing')
source=FILES/'demo 原始.txt'
run('c',source,existing,expect=1)
assert existing.read_bytes()==b'keep existing'
run('c',source,source,expect=1)
assert source.read_bytes()=='中中中AB'.encode()
run('d',FILES/'demo 压缩.huf8',existing,expect=1)
assert existing.read_bytes()==b'keep existing'
run('v',source,existing,expect=1)
repeat=FILES/'repeat.huf8'
run('c',source,repeat)
assert repeat.read_bytes()==valid

menu_c=FILES/'菜单 压缩.huf8';menu_d=FILES/'菜单 还原.txt'
run(input_text='\n'.join(['1',str(source),str(menu_c),'2',str(menu_c),str(menu_d),
                        '3',str(source),str(menu_d),'4',str(source),'0','']))
assert menu_d.read_bytes()==source.read_bytes()
run('--help');run('c',expect=2);run('unknown',expect=2)
summary={'round_trip_cases':len(cases),'independent_reference_cases':len(cases),
 'invalid_utf8_cases':len(invalid_utf8),'malformed_compressed_cases':len(bad_files),
 'unicode_and_space_paths':'passed','menu':'passed','overwrite_protection':'passed',
 'supplementary_han_and_emoji':'passed','whitespace_punctuation_bom':'passed',
 'more_than_256_character_types':'passed','cases':report}
(OUT/'test_report.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='cases'},ensure_ascii=False))
print('REPORT',OUT/'test_report.json')
