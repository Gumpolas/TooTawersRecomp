"""Read-only optional PS2 language/edition probe for the installer UI."""
from pathlib import Path
import sys,json
sys.path.insert(0,str(Path(__file__).parent))
import voces_ps2_extra
if __name__=='__main__':
 try:
  disc=voces_ps2_extra.Disc(sys.argv[1],sys.argv[2]);disc.unchanged()
  print(json.dumps({'language':sys.argv[2],'header_sha256':disc.header_sha,'status':'VERIFIED_DISC_EDITION'}))
 except Exception as error:print(str(error),file=sys.stderr);sys.exit(1)
