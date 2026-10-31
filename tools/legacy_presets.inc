// Example actions. Schematic shapes for play and study, not copies of real parts.
static const char* PRESET_TILTING = R"({"name":"Tilting barrel pistol","speed":0.02,"friction":0.12,"bodies":[
 {"id":"frame","name":"Frame","fixed":true,"ghost":true,"layer":"B","thick":22,"color":"#4d5463",
  "pts":[[-30,20],[-24,20],[-24,47.5],[70,47.5],[70,43.5],[14,43.5],[14,38.5],[195,38.5],[195,58],[74,58],[66,140],[20,140],[8,58],[-30,58]],
  "rad":[2,1,0,0,0,0,0,1,2,2,4,6,0,3],"bulge":[0,0,0,0,0,0,0,0,0,0,0,3,0,0]},
 {"id":"guard","name":"Trigger guard","fixed":true,"ghost":false,"layer":"C","thick":10,"color":"#4d5463",
  "pts":[[74,58],[85,58],[85,73],[95.7,73],[95.7,58],[99,58],[99,76],[74,76]],"rad":[0,0,0,0,0,0,3,3]},
 {"id":"rod","name":"Guide rod","fixed":true,"ghost":false,"layer":"none","thick":4,"color":"#b9bec8","pts":[[112,43],[186,43],[186,45],[112,45]],"rad":[0,0.8,0.8,0]},
 {"id":"barrel","name":"Barrel","layer":"A","thick":12,"color":"#c9ccd2",
  "pts":[[40,24],[62,24],[62,20.5],[68,20.5],[68,24],[74,24],[74,20.5],[80,20.5],[80,24],[190,24],[190,36],[72,36],[66,47],[48,47],[44,36],[40,36]],
  "rad":[0.5,0,0,0,0,0,0,0,0,0.8,0.8,0,1.5,1.5,0,0.5]},
 {"id":"mag","name":"Magazine","layer":"none","thick":13,"ghost":true,"color":"#3d434f",
  "magazine":{"cap":13,"gen":{"stack":2,"curve":0,"wall":1.2,"angle":-3.2,"top":[35,48.5]}},"pts":[[30,50],[40,50],[35,60]]},
 {"id":"magbtn","name":"Mag release button","layer":"none","thick":8,"mass":6,"color":"#e0b54a","pts":[[70,61],[76.5,61],[76.5,67],[70,67]],"rad":[0,1.5,1.5,0],
  "control":{"kind":"button","key":"magrelease","at":[76.5,64],"dir":180,"force":14}},
 {"id":"trigger","name":"Trigger","layer":"C","thick":8,"mass":8,"color":"#e0b54a",
  "pts":[[90,57.5],[95.6,57.5],[95.6,62],[95.2,68],[93.5,72],[91,72],[90,67],[89.8,62]],"rad":[0,0,0,0,1.5,1.5,0,0],"bulge":[0,0,-0.5,-0.7,0,0,0.4,0],
  "control":{"kind":"trigger","key":"fire","at":[95.4,68],"dir":180,"force":25}},
 {"id":"slide","name":"Slide","layer":"AB","thick":15,"mass":350,"ghost":true,"color":"#3f7fe0",
  "pts":[[8,8],[194,8],[194,23],[80,23],[80,20],[73.5,20],[73.5,23],[68,23],[68,20],[61.5,20],[61.5,23],[40,23],[40,38.4],[8,38.4]],
  "rad":[3,4,1,0,0,0,0,0,0,0,0,0,0,1]},
 {"id":"shoe","name":"Slide front shoe","layer":"B","thick":15,"mass":25,"ghost":true,"color":"#3f7fe0",
  "pts":[[150,37.2],[194,37.2],[194,38.4],[150,38.4]],"rad":[0,0,0.6,0]},
 {"id":"rails","name":"Slide rail lips","layer":"B","thick":15,"mass":40,"ghost":true,"color":"#3f7fe0",
  "pts":[[8,38.4],[11,38.4],[11,43.6],[62,43.6],[62,47.2],[8,47.2]],"rad":[0,0,0,0,1,1.5]}],
 "joints":[
 {"type":"weld","a":"rails","b":"slide","p":[9.5,41]},
 {"type":"weld","a":"shoe","b":"slide","p":[192,30]},
 {"type":"track","a":"slide","b":"barrel","p":[188,30],"path":[[140,30],[190,30]],"lockRot":false},
 {"type":"track","a":"frame","b":"barrel","p":[56,42],"path":[[56,42],[60,42],[67,37]],"lockRot":false},
 {"type":"spring","a":"slide","b":"frame","pa":[188,44],"pb":[110,44],"k":0.9,"rest":110,"damp":0,"kind":"coil","guide":"rod","coilDia":7,"wire":1.0,"turns":14},
 {"type":"track","a":"magbtn","b":"frame","p":[73,64],"path":[[69.5,64],[73,64]],"lockRot":true},
 {"type":"spring","a":"magbtn","b":"frame","pa":[71,64],"pb":[58,64],"k":0.5,"rest":16,"damp":2,"kind":"coil","coilDia":4,"wire":0.6,"turns":6},
 {"type":"pin","a":"trigger","b":"frame","p":[93,59.5]},
 {"type":"spring","a":"frame","b":"trigger","pa":[86.5,58.6],"pb":[89.8,64],"k":1.5,"damp":0,"kind":"leaf","preload":1.5,"flip":true},
 {"type":"magwell","a":"mag","b":"frame","p":[35,58.5],"path":[[35,58.5],[30,148]],"rel":"magbtn","releaseAt":1.5}],
 "breech":{"breech":"slide","chamber":"barrel","face":[40,30],"muzzle":[190,30],"cartridge":"9x19mm","dur":0.8,"feed":16,"rounds":13,"auto":false,"loaded":true,"feedMode":"magazine"}})";

static const char* PRESET_BLOWBACK = R"({"name":"Blowback pistol","speed":0.02,"friction":0.12,"bodies":[
 {"id":"frame","name":"Frame","fixed":true,"ghost":true,"layer":"B","thick":22,"color":"#4d5463",
  "pts":[[-30,20],[-24,20],[-24,47.5],[70,47.5],[70,43.5],[14,43.5],[14,38.5],[195,38.5],[195,58],[74,58],[66,140],[20,140],[8,58],[-30,58]],
  "rad":[2,1,0,0,0,0,0,1,2,2,4,6,0,3],"bulge":[0,0,0,0,0,0,0,0,0,0,0,3,0,0]},
 {"id":"guard","name":"Trigger guard","fixed":true,"ghost":false,"layer":"C","thick":10,"color":"#4d5463",
  "pts":[[74,58],[85,58],[85,73],[95.7,73],[95.7,58],[99,58],[99,76],[74,76]],"rad":[0,0,0,0,0,0,3,3]},
 {"id":"rod","name":"Guide rod","fixed":true,"ghost":false,"layer":"none","thick":4,"color":"#b9bec8","pts":[[112,43],[186,43],[186,45],[112,45]],"rad":[0,0.8,0.8,0]},
 {"id":"barrel","name":"Barrel","fixed":true,"ghost":false,"layer":"A","thick":12,"color":"#c9ccd2","pts":[[40,24],[190,24],[190,36],[40,36]],"rad":[0.5,0.8,0.8,0.5]},
 {"id":"mag","name":"Magazine","layer":"none","thick":13,"ghost":true,"color":"#3d434f",
  "magazine":{"cap":13,"gen":{"stack":2,"curve":0,"wall":1.2,"angle":-3.2,"top":[35,48.5]}},"pts":[[30,50],[40,50],[35,60]]},
 {"id":"magbtn","name":"Mag release button","layer":"none","thick":8,"mass":6,"color":"#e0b54a","pts":[[70,61],[76.5,61],[76.5,67],[70,67]],"rad":[0,1.5,1.5,0],
  "control":{"kind":"button","key":"magrelease","at":[76.5,64],"dir":180,"force":14}},
 {"id":"trigger","name":"Trigger","layer":"C","thick":8,"mass":8,"color":"#e0b54a",
  "pts":[[90,57.5],[95.6,57.5],[95.6,62],[95.2,68],[93.5,72],[91,72],[90,67],[89.8,62]],"rad":[0,0,0,0,1.5,1.5,0,0],"bulge":[0,0,-0.5,-0.7,0,0,0.4,0],
  "control":{"kind":"trigger","key":"fire","at":[95.4,68],"dir":180,"force":25}},
 {"id":"slide","name":"Slide","layer":"AB","thick":15,"mass":400,"ghost":true,"color":"#3f7fe0",
  "pts":[[8,8],[194,8],[194,23],[40,23],[40,38.4],[8,38.4]],"rad":[3,4,1,0,0,1]},
 {"id":"shoe","name":"Slide front shoe","layer":"B","thick":15,"mass":25,"ghost":true,"color":"#3f7fe0",
  "pts":[[150,37.2],[194,37.2],[194,38.4],[150,38.4]],"rad":[0,0,0.6,0]},
 {"id":"rails","name":"Slide rail lips","layer":"B","thick":15,"mass":40,"ghost":true,"color":"#3f7fe0",
  "pts":[[8,38.4],[11,38.4],[11,43.6],[62,43.6],[62,47.2],[8,47.2]],"rad":[0,0,0,0,1,1.5]}],
 "joints":[
 {"type":"weld","a":"rails","b":"slide","p":[9.5,41]},
 {"type":"weld","a":"shoe","b":"slide","p":[192,30]},
 {"type":"spring","a":"slide","b":"frame","pa":[188,44],"pb":[110,44],"k":1.4,"rest":118,"damp":0,"kind":"coil","guide":"rod","coilDia":7,"wire":1.0,"turns":14},
 {"type":"track","a":"magbtn","b":"frame","p":[73,64],"path":[[69.5,64],[73,64]],"lockRot":true},
 {"type":"spring","a":"magbtn","b":"frame","pa":[71,64],"pb":[58,64],"k":0.5,"rest":16,"damp":2,"kind":"coil","coilDia":4,"wire":0.6,"turns":6},
 {"type":"pin","a":"trigger","b":"frame","p":[93,59.5]},
 {"type":"spring","a":"frame","b":"trigger","pa":[86.5,58.6],"pb":[89.8,64],"k":1.5,"damp":0,"kind":"leaf","preload":1.5,"flip":true},
 {"type":"magwell","a":"mag","b":"frame","p":[35,58.5],"path":[[35,58.5],[30,148]],"rel":"magbtn","releaseAt":1.5}],
 "breech":{"breech":"slide","chamber":"barrel","face":[40,30],"muzzle":[190,30],"cartridge":".380 ACP","dur":0.6,"feed":16,"rounds":13,"auto":false,"loaded":true,"feedMode":"magazine"}})";

static const char* PRESET_LONG = R"({"name":"Long recoil","speed":0.03,"bodies":[
 {"id":"frame","name":"Receiver","fixed":true,"ghost":true,"layer":"none","thick":30,"color":"#5d6575","pts":[[-90,18],[440,18],[440,72],[-90,72]]},
 {"id":"barrel","name":"Barrel + extension","layer":"none","thick":18,"mass":2600,"color":"#c9ccd2","pts":[[150,30],[200,30],[200,38],[560,38],[560,52],[200,52],[200,60],[150,60]]},
 {"id":"mag","name":"Magazine","layer":"none","thick":16,"mass":160,"ghost":true,"color":"#3d434f","magazine":{"cap":20},"pts":[[90,72],[166,72],[166,150],[90,150]]},
 {"id":"bolt","name":"Rotating bolt","layer":"none","thick":18,"mass":640,"color":"#f0a43a","roll":{"on":true,"min":0,"max":60,"handle":0},"pts":[[60,36],[150,36],[150,54],[60,54]]}],
 "joints":[
 {"type":"track","a":"barrel","b":"frame","p":[175,45],"path":[[105,45],[175,45]],"lockRot":true},
 {"type":"track","a":"bolt","b":"frame","p":[100,45],"path":[[28,45],[100,45]],"lockRot":true},
 {"type":"spring","a":"barrel","b":"frame","pa":[200,64],"pb":[420,64],"k":3,"rest":200,"damp":0,"kind":"coil","coilDia":12,"wire":1.8,"turns":14},
 {"type":"spring","a":"bolt","b":"frame","pa":[60,45],"pb":[-80,45],"k":1.2,"rest":160,"damp":0,"kind":"coil","coilDia":10,"wire":1.4,"turns":16},
 {"type":"lug","a":"bolt","b":"barrel","unlock":40,"blockOpen":false},
 {"type":"cam","a":"bolt","b":"frame","table":[[0,0],[-55,0],[-68,60]]},
 {"type":"latch","a":"bolt","b":"frame","catchAt":-66,"rel":"barrel","releaseAt":-2},
 {"type":"magwell","a":"mag","b":"frame","p":[128,111],"path":[[128,111],[128,195]]}],
 "breech":{"breech":"bolt","chamber":"barrel","face":[150,45],"muzzle":[560,45],"cartridge":"7.62x54mmR","impulse":12,"cartImpulse":false,"dur":1.5,"feed":30,"rounds":20,"auto":true,"loaded":true,"feedMode":"magazine"}})";

static const char* PRESET_TURN = R"({"name":"Turn bolt","speed":1,"bodies":[
 {"id":"frame","name":"Receiver + barrel","fixed":true,"ghost":true,"layer":"none","thick":28,"color":"#5d6575","pts":[[0,30],[160,30],[160,39],[540,39],[540,53],[160,53],[160,62],[0,62]]},
 {"id":"mag","name":"Magazine","layer":"none","thick":18,"mass":120,"ghost":true,"color":"#3d434f","magazine":{"cap":5},"pts":[[56,62],[140,62],[140,100],[56,100]]},
 {"id":"bolt","name":"Bolt","layer":"none","thick":20,"mass":450,"damp":7,"color":"#f0a43a","roll":{"on":true,"min":0,"max":90,"handle":38},"pts":[[30,36],[150,36],[150,56],[30,56]]}],
 "joints":[
 {"type":"track","a":"bolt","b":"frame","p":[90,46],"path":[[-15,46],[90,46]],"lockRot":true},
 {"type":"lug","a":"bolt","b":"frame","unlock":75,"blockOpen":true},
 {"type":"magwell","a":"mag","b":"frame","p":[98,81],"path":[[98,81],[98,125]]}],
 "breech":{"breech":"bolt","chamber":"frame","face":[150,46],"muzzle":[540,46],"cartridge":"7.62x54mmR","impulse":11,"cartImpulse":false,"dur":1.5,"feed":70,"rounds":5,"auto":false,"loaded":true,"feedMode":"magazine"}})";

static const char* PRESET_SINGLE = R"({"name":"Single-shot rifle","speed":1,"bodies":[
 {"id":"frame","name":"Receiver + barrel","fixed":true,"ghost":true,"layer":"none","thick":26,"color":"#6b5a48","pts":[[0,30],[140,30],[140,40],[520,41],[520,51],[140,52],[140,62],[0,62]],"rad":[3,0,0,1,1,0,0,3]},
 {"id":"bolt","name":"Bolt","layer":"none","thick":18,"mass":380,"damp":7,"color":"#c9ccd2","roll":{"on":true,"min":0,"max":90,"handle":36},"pts":[[20,37],[138,37],[138,55],[20,55]],"rad":[3,1,1,3]}],
 "joints":[
 {"type":"track","a":"bolt","b":"frame","p":[80,46],"path":[[-10,46],[80,46]],"lockRot":true},
 {"type":"lug","a":"bolt","b":"frame","unlock":70,"blockOpen":true}],
 "breech":{"breech":"bolt","chamber":"frame","face":[138,46],"muzzle":[520,46],"cartridge":"5.56x45mm","dur":1.2,"feed":60,"rounds":0,"auto":false,"loaded":false,"feedMode":"single"}})";

static const char* PRESET_BLANK = R"({"name":"Untitled action","speed":0.05,"bodies":[
 {"id":"frame","name":"Frame","fixed":true,"ghost":true,"layer":"none","thick":20,"color":"#5d6575","pts":[[0,40],[220,40],[220,60],[0,60]]}],"joints":[],"breech":null})";
