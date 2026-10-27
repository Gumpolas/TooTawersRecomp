"""Inspeccion y extraccion XDVDFS de la copia local, sin modificar la ISO."""
from pathlib import Path
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parent
ISO = next(ROOT.parent.glob('*.iso'), None)

def inventory(f):
    size = __import__('os').fstat(f.fileno()).st_size
    for base in (0, 0x18300000, 0x0FD90000, 0x02080000):
        f.seek(base + 65536)
        header = f.read(36)
        if header[:20] == b'MICROSOFT*XBOX*MEDIA':
            break
    else:
        raise ValueError('No hay particion XDVDFS')
    sector, length = struct.unpack_from('<II', header, 20)
    files = []
    directories = set()
    names = set()
    def walk(sector, length, prefix, depth=0):
        if depth > 32 or not 0 < length <= 64 * 1024**2:
            raise ValueError('Directorio invalido')
        if (sector,length) in directories:
            raise ValueError('Directorio repetido/ciclico')
        directories.add((sector,length))
        if base+sector*2048+length>size:
            raise ValueError('Directorio fuera de la ISO')
        f.seek(base + sector * 2048)
        table = f.read(length)
        if len(table)!=length:
            raise ValueError('Directorio truncado')
        seen = set()
        def node(o):
            if o in seen or o + 14 > len(table):
                return
            seen.add(o)
            left, right, start, n, attr, count = struct.unpack_from('<HHIIBB', table, o)
            if left == right == 65535:
                return
            if o + 14 + count > len(table):
                raise ValueError('Nombre truncado')
            name = table[o+14:o+14+count].decode('ascii')
            if not name or any(c in name for c in '/\\:') or name in ('.','..'):
                raise ValueError('Nombre inseguro')
            if left:
                node(left * 4)
            path = prefix / name
            key=path.as_posix().lower()
            if key in names:
                raise ValueError('Ruta duplicada en ISO: '+key)
            names.add(key)
            offset = base + start * 2048
            if offset + n > size:
                raise ValueError('Fuera de la ISO')
            if attr & 16:
                walk(start, n, path, depth+1)
            else:
                files.append({'path':path.as_posix(),'offset':offset,'size':n})
            if right:
                node(right * 4)
        node(0)
    walk(sector, length, Path())
    return files

def main():
    if ISO is None:
        raise SystemExit('No se ha encontrado una ISO junto a pal-work')
    with ISO.open('rb') as f:
        files = inventory(f)
        (ROOT / 'pal_inventory.json').write_text(json.dumps(files, indent=2), encoding='utf-8')
        out = ROOT / 'pal_files'
        for item in files:
            name = item['path']
            if '--extract-all' not in sys.argv and not name.lower().endswith('.xbe'):
                continue
            dest = out / name
            if dest.exists() and dest.stat().st_size == item['size']:
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            f.seek(item['offset'])
            remaining = item['size']
            with dest.open('wb') as g:
                while remaining:
                    data = f.read(min(remaining, 8*1024**2))
                    if not data:
                        raise ValueError('Lectura incompleta')
                    g.write(data)
                    remaining -= len(data)
            print(name, item['size'], flush=True)
        for p in out.glob('*.xbe'):
            data = p.read_bytes()
            u = lambda o: struct.unpack_from('<I',data,o)[0]
            base, cert, nsec, sec = u(0x104), u(0x118), u(0x11c), u(0x120)
            co = cert-base
            title = data[co+12:co+92].decode('utf-16-le').rstrip('\0')
            print(p.name,'SHA256',hashlib.sha256(data).hexdigest(),'title',title,'region',hex(u(co+0xa0)))
            print('base',hex(base),'entry retail',hex(u(0x128)^0xA8FC57AB))
            for i in range(nsec):
                o = sec-base+56*i
                flags, va, vs, raw, rs, no = struct.unpack_from('<6I',data,o)
                name = data[no-base:].split(b'\0')[0].decode('ascii')
                print(name,hex(va),hex(vs),hex(raw),hex(rs))
    print('Inventario',len(files),'archivos',sum(x['size'] for x in files),'bytes')
    print('Raiz:',[x['path'] for x in files if '/' not in x['path']])

if __name__ == '__main__':
    main()
