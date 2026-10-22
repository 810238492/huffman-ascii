"""ASCII哈夫曼程序的独立参考编码、指标、往返与异常输入测试。仅用标准库。"""
from pathlib import Path
from collections import Counter
import heapq,json,math,os,random,re,struct,subprocess,uuid,hashlib

CODE=Path(__file__).resolve().parents[1]
ROOT=CODE.parent
EXE=CODE/('huffman_ascii.exe' if os.name=='nt' else 'huffman_ascii')
OUT=ROOT/'tmp'/'ascii_huffman_tests'/uuid.uuid4().hex[:10]
FILES=OUT/'中文 空格路径';FILES.mkdir(parents=True)

def run(*args,input_text=None,expect=0):
    p=subprocess.run([str(EXE),*map(str,args)],cwd=CODE,input=input_text,
        capture_output=True,encoding='utf-8',timeout=30,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    assert p.returncode==expect,(args,p.returncode,p.stdout,p.stderr)
    return p

def reference(data):
    freq=Counter(data);heap=[];codes={}
    for i,c in enumerate(sorted(freq),1):heapq.heappush(heap,(freq[c],i,('leaf',c)))
    if len(heap)==1:codes[heap[0][2][1]]='0'
    elif heap:
        k=len(heap)+1
        while len(heap)>1:
            a,_,left=heapq.heappop(heap);b,_,right=heapq.heappop(heap)
            heapq.heappush(heap,(a+b,k,('branch',left,right)));k+=1
        def walk(node,path):
            if node[0]=='leaf':codes[node[1]]=path
            else:walk(node[1],path+'0');walk(node[2],path+'1')
        walk(heap[0][2],'')
    bits=''.join(codes[c] for c in data)
    padded=bits+'0'*(-len(bits)%8)
    header=struct.pack('<QI',len(data),len(freq))
    header+=b''.join(struct.pack('<BI',c,freq[c]) for c in sorted(freq))
    payload=bytes(int(padded[i:i+8],2) for i in range(0,len(padded),8))
    wpl=len(bits) if len(freq)>1 else 0
    avg=len(bits)/len(data) if data else None
    entropy=-sum((n/len(data))*math.log2(n/len(data)) for n in freq.values()) if data else None
    return header+payload,codes,wpl,avg,entropy

rng=random.Random(20261022)
cases=[('empty',b''),('single_A',b'A'),('single_nul',b'\0'*17),
       ('single_space',b' '*1024),('single_del',b'\x7f'*9),
       ('all_128',bytes(range(128))*3),('ties',b'ABCDEFGH'*17),
       ('newlines',b'A\r\nB\nC\rD\t !?\0\x7f'),('demo',b'A'*3+b'B'*3+b'C'*6+b'D'*9),
       ('large',b'ASCII Huffman test 0123\r\n\t'*10000)]
for i in range(24):
    alphabet=rng.sample(range(128),rng.randint(1,128))
    cases.append((f'random_{i:02d}',bytes(rng.choice(alphabet) for _ in range(rng.randint(0,4000)))))
for p in sorted((CODE/'testdata').glob('*.txt')):cases.append((p.stem,p.read_bytes()))

results=[]
for name,data in cases:
    src=FILES/(name+' 原始.txt');packed=FILES/(name+' 压缩.huf');dst=FILES/(name+' 还原.txt')
    src.write_bytes(data);expected,codes,wpl,avg,entropy=reference(data)
    run('c',src,packed)
    assert packed.read_bytes()==expected,name
    run('d',packed,dst);assert dst.read_bytes()==data,name
    run('v',src,dst)
    shown=run('t',src).stdout
    assert int(re.search(r'WPL：(\d+)',shown)[1])==wpl,(name,shown)
    if data:
        assert abs(float(re.search(r'平均码长：([\d.]+)',shown)[1])-avg)<0.00000051
        assert abs(float(re.search(r'信息熵：([\d.]+)',shown)[1])-entropy)<0.00000051
    else:assert '平均码长：—' in shown and '信息熵：—' in shown
    rows=[line for line in shown.splitlines() if re.match(r'^\d+\s+',line)]
    assert len(rows)==len(codes)
    for row in rows:
        number,label,count,length,code=row.split()
        c=int(number);assert int(count)==data.count(c)
        assert code==codes[c] and int(length)==len(code)
    results.append({'case':name,'original_bytes':len(data),'kinds':len(codes),
        'compressed_bytes':len(expected),'WPL':wpl,'average_length':avg,'entropy':entropy,
        'compression_ratio':len(expected)/len(data)*100 if data else None,
        'roundtrip':True,'reference_bytes_match':True,'sha256':hashlib.sha256(data).hexdigest()})
    if name=='demo':(OUT/'demo_codes.txt').write_text(shown,encoding='utf-8')

invalid=[b'\x80',b'\xff',b'ABC\x80',b'\xef\xbb\xbfABC',b'\xff\xfeA\0','中文'.encode('utf-8')]
for i,data in enumerate(invalid):
    src=FILES/f'invalid_{i}.txt';dst=FILES/f'invalid_{i}.huf';src.write_bytes(data)
    run('c',src,dst,expect=1);assert not dst.exists();run('t',src,expect=1)

valid=reference(b'A'*3+b'B'*3+b'C'*6+b'D'*9)[0]
bad={f'truncated_{i}':valid[:i] for i in range(len(valid))}
bad.update({
 'types_129':struct.pack('<QI',0,129),
 'non_ascii':struct.pack('<QI',1,1)+struct.pack('<BI',128,1)+b'\0',
 'zero_freq':struct.pack('<QI',1,1)+struct.pack('<BI',65,0)+b'\0',
 'wrong_total':struct.pack('<QI',2,1)+struct.pack('<BI',65,1)+b'\0',
 'duplicate':struct.pack('<QI',2,2)+struct.pack('<BI',65,1)*2+b'\0',
 'unsorted':struct.pack('<QI',2,2)+struct.pack('<BI',66,1)+struct.pack('<BI',65,1)+b'\0',
 'trailing_byte':valid+b'\0',
 'nonzero_padding':reference(b'A')[0][:-1]+b'\x01',
 'single_nonzero':reference(b'A')[0][:-1]+b'\x80',
 'empty_with_data':struct.pack('<QI',0,0)+b'\0',
 'wrong_histogram':struct.pack('<QI',2,2)+struct.pack('<BI',65,1)+struct.pack('<BI',66,1)+b'\0',
})
for name,data in bad.items():
    src=FILES/(name+'.huf');dst=FILES/(name+'_bad.txt');src.write_bytes(data)
    run('d',src,dst,expect=1);assert not dst.exists(),name

src=FILES/'demo 原始.txt';packed=FILES/'demo 压缩.huf'
guard=FILES/'existing.txt';guard.write_bytes(b'KEEP')
run('c',src,guard,expect=1);run('d',packed,guard,expect=1);assert guard.read_bytes()==b'KEEP'
digest=src.read_bytes();run('c',src,src,expect=1);assert src.read_bytes()==digest
run('v',src,guard,expect=1);run('c',FILES/'missing.txt',FILES/'missing.huf',expect=1)
run('--help');run('unknown',expect=2);run('c',expect=2)
menu=run(input_text=f'4\n{src}\n0\n').stdout;assert 'WPL：39' in menu
report={'roundtrip_cases':len(results),'invalid_ascii_cases':len(invalid),
        'corrupt_file_cases':len(bad),'results':results,
        'checks':['exact_reference_bytes','metrics','128_ASCII_values','Chinese_space_paths',
                  'menu','existing_output_protection','same_path_protection','invalid_commands']}
(OUT/'test_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(f'PASS: {len(results)} roundtrips, {len(invalid)} invalid ASCII inputs, {len(bad)} corrupt files.')
print(OUT/'test_report.json')
