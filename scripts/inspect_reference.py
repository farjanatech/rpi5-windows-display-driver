# Research-only; no target-machine execution or driver loading.
import gzip, hashlib, json, pathlib, struct, subprocess, urllib.request, uuid
out=pathlib.Path('inspection');out.mkdir(exist_ok=True)
work=pathlib.Path('inspection-private');work.mkdir(exist_ok=True)
def get(url):
    with urllib.request.urlopen(url, timeout=45) as r:
        b=r.read(67108865)
        if len(b)>67108864: raise ValueError('Oversized reference')
        return b
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
for name in ('dxgkrnl.sys','basicdisplay.sys'):
    url='https://raw.githubusercontent.com/m417z/winbindex-data-arm64/gh-pages/by_filename_compressed/'+name+'.json.gz'
    index=json.loads(gzip.decompress(get(url)));candidates=[]
    for digest,entry in index.items():
        f=entry.get('fileInfo',{});v=f.get('version','').split(' ')[0].split('.')
        try: nums=tuple(map(int,v))
        except ValueError: continue
        if len(nums)==4 and nums[:3]==(10,0,22621) and nums[3]<=4317 and f.get('machineType')==43620 and 'timestamp' in f and 'virtualSize' in f:
            candidates.append((nums,digest,f))
    if not candidates: raise RuntimeError('No indexed ARM64 reference for '+name)
    version,digest,f=max(candidates)
    symbol_id=f'{f["timestamp"]:08X}{f["virtualSize"]:x}'
    binary_url=f'https://msdl.microsoft.com/download/symbols/{name}/{symbol_id}/{name}'
    binary=get(binary_url);actual=hashlib.sha256(binary).hexdigest()
    metadata={'index_url':url,'microsoft_binary_url':binary_url,'fileInfo':f,'index_sha256':digest,'download_sha256':actual,'prefix':binary[:16].hex(),'reference_only_not_user_binary':True}
    (out/(name+'.json')).write_text(json.dumps(metadata,indent=2))
    if binary[:2]!=b'MZ': raise RuntimeError('Not a PE image')
    pe=u32(binary,0x3c);opt=pe+24
    if binary[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',binary,pe+4)[0]!=43620 or u32(binary,pe+8)!=f['timestamp'] or u32(binary,opt+56)!=f['virtualSize']:
        raise RuntimeError('Reference image metadata mismatch')
    target=work/name;target.write_bytes(binary)
    # Symbol server can return a differently signed copy with the same image identity.
    # Do not treat index hash as equivalent: require an actual Microsoft Authenticode chain.
    check="$ErrorActionPreference='Stop';$s=Get-AuthenticodeSignature '"+str(target.resolve())+"';$s|Format-List|Out-String|Write-Host;if($s.Status -ne 'Valid' -or $s.SignerCertificate.Subject -notlike '*Microsoft*'){exit 1}"
    verified=subprocess.run(['pwsh.exe','-NoProfile','-NonInteractive','-Command',check],capture_output=True,text=True,timeout=90)
    (out/(name+'.signature.txt')).write_text(verified.stdout+verified.stderr)
    if verified.returncode: raise RuntimeError('Microsoft signature verification failed')
    sections=[]
    for i in range(struct.unpack_from('<H',binary,pe+6)[0]):
        p=opt+struct.unpack_from('<H',binary,pe+20)[0]+40*i
        sections.append((u32(binary,p+12),u32(binary,p+16),u32(binary,p+20)))
    def raw(rva):
        for va,n,p in sections:
            if va<=rva<va+n: return p+rva-va
        raise ValueError('RVA')
    debug=u32(binary,opt+112+6*8);size=u32(binary,opt+116+6*8);pdb_path=None
    for p in range(raw(debug),raw(debug)+size,28):
        if u32(binary,p+12)!=2: continue
        cv=u32(binary,p+24)
        if binary[cv:cv+4]!=b'RSDS': continue
        guid=uuid.UUID(bytes_le=binary[cv+4:cv+20]).hex.upper();age=u32(binary,cv+20)
        pdb_name=binary[cv+24:binary.index(b'\0',cv+24)].decode().split('\\')[-1]
        pdb_url=f'https://msdl.microsoft.com/download/symbols/{pdb_name}/{guid}{age:x}/{pdb_name}'
        pdb_path=work/pdb_name;pdb_path.write_bytes(get(pdb_url));metadata['pdb_url']=pdb_url
        break
    metadata['sections']=sections
    (out/(name+'.json')).write_text(json.dumps(metadata,indent=2))
    llvm=pathlib.Path(r'C:\Program Files\LLVM\bin')
    with (out/(name+'.disassembly.txt')).open('w') as stream:
        subprocess.run([str(llvm/'llvm-objdump.exe'),'-d',str(target)],stdout=stream,check=True,timeout=90)
    if pdb_path:
        with (out/(name+'.symbols.txt')).open('w') as stream:
            subprocess.run([str(llvm/'llvm-pdbutil.exe'),'dump','-publics',str(pdb_path)],stdout=stream,check=True,timeout=90)
    print(name,version,actual)
