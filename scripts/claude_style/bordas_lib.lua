-- ============================================================================
-- MOTOR DE BORDAS (Claude). Fica em _G.B e PERSISTE entre chamadas de run_lua
-- (mas morre quando o editor fecha). CARREGAR NO COMECO DA SESSAO assim:
--
--   local src = app.readFile("claude_style/bordas_lib.lua")
--   load(src, "bordas_lib", "t", _ENV)()      -- o _ENV e OBRIGATORIO; sem ele da
--                                             -- "attempt to index a nil value (upvalue '_ENV')"
-- Testado em 23/11/2026: carrega e reproduz o canvas de 1222,1314 com 0 erros.
-- As regras estao explicadas em claude_style/bordas-regras.md. Aqui e so o codigo.
--
-- USO TIPICO:
--   1) B.defSet("mush",{18053,18054,18055},{n=18106,...})      -- um por chao com borda
--      B.defSet("path",{23324,23325,23326},nil)                 -- chao SEM conjunto de borda
--   2) B.load(x0,y0,x1,y1)                                      -- le o chao para B.C
--   3) B.cleanGround(x0,y0,x1,y1)                               -- tira armadilhas/pinos/picos
--   4) B.writeGround(x0,y0,x1,y1)                               -- grava o chao consertado
--   5) ok,A = B.solveSet{ground=B.isc("mush"), recv=fn, x0=..,y0=..,x1=..,y1=..,
--                        free=B.freeOut(x0,y0,x1,y1), budget=200000}
--   6) B.clearPieces(x0,y0,x1,y1) ; B.apply(A,"mush")
--   7) B.verify("mush","mush",recv,x0,y0,x1,y1)  -> 0 erros
--      e o check_region do editor (os "floating border pieces" dele sao os pininhos da R10:
--      FALSO POSITIVO, nao consertar).
-- ============================================================================
_G.B = {}
local B = _G.B

-- lados onde cada peca desenha linha INTEIRA (quinas nao desenham lado nenhum)
B.ARM = {n={t=1}, s={b=1}, e={r=1}, w={l=1},
         dnw={t=1,l=1}, dne={t=1,r=1}, dsw={b=1,l=1}, dse={b=1,r=1},
         cnw={}, cne={}, csw={}, cse={}}
B.STR = {n=1,e=1,s=1,w=1}
B.DIA = {dnw=1,dne=1,dsw=1,dse=1}
B.QUI = {cnw=1,cne=1,csw=1,cse=1}
-- R5: passos de escadinha. Os 8 primeiros sao DELE; os outros 8 sao inferencia minha
-- (3 retas iguais igualmente espacadas) e ele ainda nao confirmou.
B.STEPS_MAPPER = {{2,1},{2,-1},{-2,1},{-2,-1},{1,2},{1,-2},{-1,2},{-1,-2}}
B.STEPS = {{2,1},{2,-1},{-2,1},{-2,-1},{1,2},{1,-2},{-1,2},{-1,-2},
           {1,1},{1,-1},{-1,1},{-1,-1},{2,0},{-2,0},{0,2},{0,-2}}

B.SET, B.CLASS, B.IDW = {}, {}, {}
function B.key(x,y) return x*100000+y end
local K = B.key

-- define um chao: classe, ids de chao, tabela de pecas (ou nil se o chao nao tem conjunto)
function B.defSet(cls, groundIds, pieces)
  B.SET[cls] = {ids=groundIds, p=pieces}
  for _,id in ipairs(groundIds) do B.CLASS[id]=cls end
  local w={} for _,id in ipairs(groundIds) do w[#w+1]={id,1} end
  B.IDW[cls]=w
end
-- pesa os ids pela frequencia que ja existe numa area (para o chao novo ficar igual)
function B.learnWeights(x0,y0,x1,y1)
  local cnt={}
  for y=y0,y1 do for x=x0,x1 do
    local t=app.map:getTile(x,y,7)
    if t and t.ground then
      local c=B.CLASS[t.ground.id]
      if c then cnt[c]=cnt[c] or {} cnt[c][t.ground.id]=(cnt[c][t.ground.id] or 0)+1 end
    end
  end end
  for c,t in pairs(cnt) do
    local w={} for id,n in pairs(t) do w[#w+1]={id,n} end
    B.IDW[c]=w
  end
end
function B.randId(cls)
  local w=B.IDW[cls]
  local tot=0 for _,e in ipairs(w) do tot=tot+e[2] end
  local r=math.random(tot)
  for _,e in ipairs(w) do r=r-e[2] if r<=0 then return e[1] end end
  return w[1][1]
end

function B.load(x0,y0,x1,y1,z)
  z=z or 7
  B.C={} B.z=z
  local n=0
  for y=y0,y1 do for x=x0,x1 do
    local t=app.map:getTile(x,y,z)
    if t and t.ground then
      local c=B.CLASS[t.ground.id]
      if c then B.C[K(x,y)]=c n=n+1 end
    end
  end end
  return n
end
function B.cls(x,y) return B.C[K(x,y)] end
function B.isc(cl) return function(x,y) return B.C[K(x,y)]==cl end end
function B.freeOut(x0,y0,x1,y1)
  return function(x,y) return x<x0 or x>x1 or y<y0 or y>y1 end
end
function B.grid(x0,y0,x1,y1,letters)
  letters = letters or {}
  for y=y0,y1 do
    local r=""
    for x=x0,x1 do
      local c=B.C[K(x,y)]
      r=r..(c and (letters[c] or c:sub(1,1)) or ".")
    end
    print(y.." "..r)
  end
end

-- ---------------------------------------------------------------- CHAO: problemas
-- ARMADILHA R4: duas diagonais FORCADAS do mesmo tipo vizinhas na diagonal.
-- isA = "e o chao dono"; tudo que nao e isA conta como receptor (inclusive o vazio).
function B.traps(isA,x0,y0,x1,y1)
  local out={}
  local function V(x,y) return not isA(x,y) end
  for y=y0,y1 do for x=x0,x1 do
    if V(x,y) then
      if isA(x,y-1) and isA(x-1,y) and V(x-1,y+1) and isA(x-2,y+1) and V(x,y+1) then out[#out+1]={"dnw",x,y} end
      if isA(x,y-1) and isA(x+1,y) and V(x+1,y+1) and isA(x+2,y+1) and V(x,y+1) then out[#out+1]={"dne",x,y} end
      if isA(x,y+1) and isA(x-1,y) and V(x-1,y-1) and isA(x-2,y-1) and V(x,y-1) then out[#out+1]={"dsw",x,y} end
      if isA(x,y+1) and isA(x+1,y) and V(x+1,y-1) and isA(x+2,y-1) and V(x,y-1) then out[#out+1]={"dse",x,y} end
    end
  end end
  return out
end
function B.classes()
  local t={} for c,_ in pairs(B.SET) do t[#t+1]=c end return t
end
-- lista tudo que impede a borda de fechar: armadilha R4, pino de chao, pico de 1 tile
function B.groundProblems(x0,y0,x1,y1)
  local out={}
  for _,cl in ipairs(B.classes()) do
    local isA=B.isc(cl)
    for _,tr in ipairs(B.traps(isA,x0,y0,x1,y1)) do out[#out+1]={tr[2],tr[3],"R4 "..cl} end
    for y=y0,y1 do for x=x0,x1 do
      if not isA(x,y) then
        local a,b,c,d=isA(x,y-1),isA(x,y+1),isA(x+1,y),isA(x-1,y)
        local n=(a and 1 or 0)+(b and 1 or 0)+(c and 1 or 0)+(d and 1 or 0)
        if n>=3 or (a and b) or (c and d) then out[#out+1]={x,y,"pino "..cl} end
      end
    end end
  end
  for y=y0,y1 do for x=x0,x1 do
    local c=B.C[K(x,y)]
    if c then
      local n=0
      for _,d in ipairs({{1,0},{-1,0},{0,1},{0,-1}}) do if B.C[K(x+d[1],y+d[2])]==c then n=n+1 end end
      if n<=1 then out[#out+1]={x,y,"pico "..c} end
    end
  end end
  return out
end
-- conserto: troca tiles de beirada (so por classe que JA encosta neles, ou vazio) ate zerar
function B.cleanGround(x0,y0,x1,y1,passes)
  passes=passes or 8
  local function nloc(x,y,r) return #B.groundProblems(x-r,y-r,x+r,y+r) end
  for pass=1,passes do
    local p=B.groundProblems(x0,y0,x1,y1)
    if #p==0 then return 0,pass end
    for _,pr in ipairs(p) do
      local x,y=pr[1],pr[2]
      local bn,bk,bv=nloc(x,y,4),nil,nil
      local cands={{x,y},{x,y-1},{x,y+1},{x-1,y},{x+1,y},{x-1,y-1},{x+1,y-1},{x-1,y+1},{x+1,y+1}}
      for _,c in ipairs(cands) do
        local k=K(c[1],c[2])
        local old=B.C[k]
        local nb={}
        for _,d in ipairs({{1,0},{-1,0},{0,1},{0,-1}}) do nb[#nb+1]=B.C[K(c[1]+d[1],c[2]+d[2])] end
        nb[#nb+1]=false                                   -- false = virar vazio
        for _,nv in ipairs(nb) do
          local val = nv or nil                           -- CUIDADO: nunca use "cond and nil or x"
          if val~=old then
            B.C[k]=val
            local n=nloc(x,y,4)
            if n<bn then bn=n bk=k bv=val end
            B.C[k]=old
          end
        end
      end
      if bk then B.C[bk]=bv end
    end
  end
  return #B.groundProblems(x0,y0,x1,y1),passes
end
function B.writeGround(x0,y0,x1,y1)
  local z=B.z or 7
  local set,rem=0,0
  for y=y0,y1 do for x=x0,x1 do
    local want=B.C[K(x,y)]
    local t=app.map:getTile(x,y,z)
    if want then
      local tt=t or app.map:getOrCreateTile(x,y,z)
      if not tt.ground or B.CLASS[tt.ground.id]~=want then tt.ground=B.randId(want) set=set+1 end
    elseif t and t.ground then t.ground=nil rem=rem+1 end
  end end
  return set,rem
end

-- ---------------------------------------------------------------- SOLVER
-- opcoes por tile: quem tem 1 contato cardinal escolhe entre a reta e as 2 diagonais que a
-- contem; 2 contatos perpendiculares = diagonal FORCADA; so contato diagonal = pode ficar sem
-- peca (a quina sai da R10) ou tomar uma das pecas daquele canto.
function B.mkTiles(cfg)
  local tiles,ring={},{}
  local G,R=cfg.ground,cfg.recv
  local D={se={"e","s","dse"},sw={"w","s","dsw"},ne={"e","n","dne"},nw={"w","n","dnw"}}
  for y=cfg.y0,cfg.y1 do for x=cfg.x0,cfg.x1 do
    if R(x,y) and not G(x,y) then
      local N,S,E,W=G(x,y-1),G(x,y+1),G(x+1,y),G(x-1,y)
      local opts
      if N and W then opts={{"dnw"}} elseif N and E then opts={{"dne"}}
      elseif S and W then opts={{"dsw"}} elseif S and E then opts={{"dse"}}
      elseif N then opts={{"n"},{"dnw"},{"dne"}} elseif S then opts={{"s"},{"dsw"},{"dse"}}
      elseif E then opts={{"e"},{"dne"},{"dse"}} elseif W then opts={{"w"},{"dnw"},{"dsw"}}
      else
        local dg={}
        if G(x-1,y-1) then dg[#dg+1]="nw" end
        if G(x+1,y-1) then dg[#dg+1]="ne" end
        if G(x-1,y+1) then dg[#dg+1]="sw" end
        if G(x+1,y+1) then dg[#dg+1]="se" end
        if #dg>0 then
          opts={{}}
          for _,d in ipairs(dg) do for _,p in ipairs(D[d]) do opts[#opts+1]={p} end end
        end
      end
      if opts then
        local o={}
        for _,b in ipairs(opts) do local m={} for _,p in ipairs(b) do m[p]=true end o[#o+1]=m end
        tiles[#tiles+1]={x=x,y=y,opts=o}
        ring[K(x,y)]=true
      end
    end
  end end
  -- ORDEM: por trecho conectado, andando pelo contorno (BFS). Linha-a-linha faz o
  -- backtracking explodir (400k nos sem fechar vs 2,3k com esta ordem).
  if cfg.noorder then return tiles,ring end
  local byk={} for _,t in ipairs(tiles) do byk[K(t.x,t.y)]=t end
  local seen,out={},{}
  local DIR={{1,0},{0,1},{-1,0},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}
  for _,t in ipairs(tiles) do
    local k=K(t.x,t.y)
    if not seen[k] then
      seen[k]=true
      local q={t} local qi=1
      while qi<=#q do
        local c=q[qi] qi=qi+1
        out[#out+1]=c
        for _,d in ipairs(DIR) do
          local nk=K(c.x+d[1],c.y+d[2])
          if byk[nk] and not seen[nk] then seen[nk]=true q[#q+1]=byk[nk] end
        end
      end
    end
  end
  return out,ring
end

function B.solveSet(cfg)
  local tiles,ring=B.mkTiles(cfg)
  if cfg.pre then
    for _,t in ipairs(tiles) do
      local p=cfg.pre[K(t.x,t.y)]
      if p then t.opts={p} end
    end
  end
  local A,Q={},{}
  local G,R,FREE=cfg.ground,cfg.recv,(cfg.free or function() return false end)
  local S=cfg.steps or B.STEPS
  local ARM=B.ARM
  local function decided(x,y) return A[K(x,y)]~=nil or not ring[K(x,y)] end
  local function armOf(x,y,s)
    local t=A[K(x,y)] if not t then return false end
    for p,_ in pairs(t) do local a=ARM[p] if a and a[s] then return true end end
    return false
  end
  local function segV(x,y) return armOf(x,y,"l") or armOf(x-1,y,"r") end
  local function segH(x,y) return armOf(x,y,"t") or armOf(x,y-1,"b") end
  -- R10: em cada vertice com ao menos 1 segmento, todo quadrante VAZIO nao encostado
  -- recebe a quina que aponta para o vertice.
  local function calcQ(x,y)
    if G(x,y) or not R(x,y) then return nil end
    local q=nil
    for _,v in ipairs({{x,y,"cnw"},{x+1,y,"cne"},{x,y+1,"csw"},{x+1,y+1,"cse"}}) do
      local vx,vy,nm=v[1],v[2],v[3]
      local all=true
      for dy=-1,0 do for dx=-1,0 do
        local a,b=vx+dx,vy+dy
        if FREE(a,b) or not (G(a,b) or decided(a,b)) then all=false end
      end end
      if all then
        local N,Sg,Wa,E=segV(vx,vy-1),segV(vx,vy),segH(vx-1,vy),segH(vx,vy)
        if N or Sg or Wa or E then
          local t
          if nm=="cnw" then t=(Sg or E) elseif nm=="cne" then t=(Sg or Wa)
          elseif nm=="csw" then t=(N or E) else t=(N or Wa) end
          if not t then q=q or {} q[nm]=true end
        end
      end
    end
    return q
  end
  local function updQ(x,y) for yy=y-1,y+1 do for xx=x-1,x+1 do Q[K(xx,yy)]=calcQ(xx,yy) end end end
  local function f(x,y,p)
    local t=A[K(x,y)]
    if t and t[p] then return true end
    local q=Q[K(x,y)]
    return q~=nil and q[p]==true
  end
  local function fH(x,y) local t=A[K(x,y)] return t~=nil and (t.n or t.s)==true end
  local function fV(x,y) local t=A[K(x,y)] return t~=nil and (t.e or t.w)==true end
  local function anchorBad(x,y)
    if fH(x,y) and fH(x+1,y) then return true end            -- R1 reta+reta horizontal
    if fV(x,y) and fV(x,y+1) then return true end            -- R1 reta+reta vertical
    local q=Q[K(x,y)]
    if q then
      -- R2 quadrado, R4 duas diagonais, R6 (padrao EXATO dos 4 bicos no mesmo vertice)
      if q.cse then
        if (f(x+1,y,"s") and f(x,y+1,"e")) or (f(x,y+1,"dse") and f(x+1,y,"dse")) then return true end
        if f(x+1,y,"csw") and f(x,y+1,"cne") and f(x+1,y+1,"cnw") then return true end
      end
      if q.csw and ((f(x-1,y,"s") and f(x,y+1,"w")) or (f(x,y+1,"dsw") and f(x-1,y,"dsw"))) then return true end
      if q.cne and ((f(x+1,y,"n") and f(x,y-1,"e")) or (f(x,y-1,"dne") and f(x+1,y,"dne"))) then return true end
      if q.cnw and ((f(x-1,y,"n") and f(x,y-1,"w")) or (f(x,y-1,"dnw") and f(x-1,y,"dnw"))) then return true end
    end
    local t=A[K(x,y)]
    if t then                                                -- R3 hexagono
      if t.s and f(x-1,y+1,"dse") and f(x+1,y+1,"dsw") then return true end
      if t.n and f(x-1,y-1,"dne") and f(x+1,y-1,"dnw") then return true end
      if t.w and f(x-1,y-1,"dsw") and f(x-1,y+1,"dnw") then return true end
      if t.e and f(x+1,y-1,"dse") and f(x+1,y+1,"dne") then return true end
    end
    return false
  end
  -- R9: as linhas tem que casar na divisa entre dois tiles que nao sao o chao dono
  local function raPair(ax,ay,bx,by,sa,sb)
    if G(ax,ay) or G(bx,by) then return false end
    if FREE(ax,ay) or FREE(bx,by) then return false end
    if not (decided(ax,ay) and decided(bx,by)) then return false end
    return armOf(ax,ay,sa)~=armOf(bx,by,sb)
  end
  local function bad(x,y)
    if raPair(x,y,x+1,y,"r","l") then return true end
    if raPair(x-1,y,x,y,"r","l") then return true end
    if raPair(x,y,x,y+1,"b","t") then return true end
    if raPair(x,y-1,x,y,"b","t") then return true end
    for yy=y-2,y+2 do for xx=x-2,x+2 do if anchorBad(xx,yy) then return true end end end
    local t=A[K(x,y)]
    if t then for _,p in ipairs({"n","e","s","w"}) do if t[p] then     -- R5 escadinha
      for _,d in ipairs(S) do local dx,dy=d[1],d[2]
        if (f(x+dx,y+dy,p) and f(x+2*dx,y+2*dy,p))
        or (f(x-dx,y-dy,p) and f(x+dx,y+dy,p))
        or (f(x-dx,y-dy,p) and f(x-2*dx,y-2*dy,p)) then return true end
      end end end end
    return false
  end
  local nodes,limit=0,cfg.budget or 200000
  local deep=0
  local function rec(i)
    if i>#tiles then return true end
    if i>deep then deep=i end
    local t=tiles[i] local k=K(t.x,t.y)
    local ord={} for j=1,#t.opts do ord[j]=j end
    if cfg.rand then for j=#ord,2,-1 do local r=math.random(j) ord[j],ord[r]=ord[r],ord[j] end end
    for _,j in ipairs(ord) do
      nodes=nodes+1
      if nodes>limit then return false end
      A[k]=t.opts[j] updQ(t.x,t.y)
      if not bad(t.x,t.y) then if rec(i+1) then return true end end
      A[k]=nil updQ(t.x,t.y)
    end
    return false
  end
  local ok=rec(1)
  if ok then   -- junta os pininhos derivados (inclusive nos tiles sem contato: as pontas)
    for y=cfg.y0-1,cfg.y1+1 do for x=cfg.x0-1,cfg.x1+1 do
      local q=calcQ(x,y)
      if q then local a=A[K(x,y)] or {} for p,_ in pairs(q) do a[p]=true end A[K(x,y)]=a end
    end end
  end
  return ok,A,tiles,tiles[math.min(deep,math.max(#tiles,1))],nodes
end

-- ---------------------------------------------------------------- escrever / conferir
function B.clearPieces(x0,y0,x1,y1)
  local z=B.z or 7
  local NM={}
  for _,s in pairs(B.SET) do if s.p then for _,id in pairs(s.p) do NM[id]=true end end end
  local n=0
  for y=y0,y1 do for x=x0,x1 do
    local t=app.map:getTile(x,y,z)
    if t and t.itemCount>0 then
      for i=#t.items,1,-1 do if NM[t.items[i].id] then t:removeItem(t.items[i]) n=n+1 end end
    end
  end end
  return n
end
function B.apply(A,setname)
  local z=B.z or 7
  local P=B.SET[setname].p
  local n=0
  local ord={"n","e","s","w","dnw","dne","dsw","dse","cnw","cne","csw","cse"}
  for k,t in pairs(A) do
    local x=math.floor(k/100000) local y=k-x*100000
    local tile=app.map:getOrCreateTile(x,y,z)
    for _,p in ipairs(ord) do if t[p] then tile:addItem(P[p]) n=n+1 end end
  end
  return n
end
-- verificador independente: le o MAPA, nao a memoria do solver
function B.verify(setname,gcls,recvFn,x0,y0,x1,y1)
  local z=B.z or 7
  local ARM=B.ARM
  local P=B.SET[setname].p
  local NM={} for nm,id in pairs(P) do NM[id]=nm end
  local pieces={}
  for y=y0-2,y1+2 do for x=x0-2,x1+2 do
    local t=app.map:getTile(x,y,z)
    if t then
      local l={} for _,it in ipairs(t.items) do if NM[it.id] then l[#l+1]=NM[it.id] end end
      if #l>0 then pieces[K(x,y)]=l end
    end
  end end
  local function G(x,y) return B.cls(x,y)==gcls end
  local R=recvFn
  local function f(x,y,p) local l=pieces[K(x,y)] if not l then return false end
    for _,v in ipairs(l) do if v==p then return true end end return false end
  local function fA(x,y,set) local l=pieces[K(x,y)] if not l then return false end
    for _,v in ipairs(l) do for _,q in ipairs(set) do if v==q then return true end end end return false end
  local function arm(x,y,s) local l=pieces[K(x,y)] if not l then return false end
    for _,p in ipairs(l) do if ARM[p] and ARM[p][s] then return true end end return false end
  local V={} local function add(m) V[#V+1]=m end
  local HOR={"n","s"} local VER={"e","w"}
  for y=y0,y1 do for x=x0,x1 do
    if R(x,y) and not G(x,y) then
      if G(x,y-1) and not fA(x,y,{"n","dnw","dne"}) then add("R7 "..x..","..y.." N") end
      if G(x,y+1) and not fA(x,y,{"s","dsw","dse"}) then add("R7 "..x..","..y.." S") end
      if G(x+1,y) and not fA(x,y,{"e","dne","dse"}) then add("R7 "..x..","..y.." L") end
      if G(x-1,y) and not fA(x,y,{"w","dnw","dsw"}) then add("R7 "..x..","..y.." O") end
      if R(x+1,y) and not G(x+1,y) and (arm(x,y,"r")~=arm(x+1,y,"l")) then add("R9 "..x..","..y.."|"..(x+1)..","..y) end
      if R(x,y+1) and not G(x,y+1) and (arm(x,y,"b")~=arm(x,y+1,"t")) then add("R9 "..x..","..y.."|"..x..","..(y+1)) end
    end
    if fA(x,y,HOR) and fA(x+1,y,HOR) then add("R1h "..x..","..y) end
    if fA(x,y,VER) and fA(x,y+1,VER) then add("R1v "..x..","..y) end
    if f(x,y,"cse") and f(x+1,y,"s") and f(x,y+1,"e") then add("R2 "..x..","..y) end
    if f(x,y,"csw") and f(x-1,y,"s") and f(x,y+1,"w") then add("R2 "..x..","..y) end
    if f(x,y,"cne") and f(x+1,y,"n") and f(x,y-1,"e") then add("R2 "..x..","..y) end
    if f(x,y,"cnw") and f(x-1,y,"n") and f(x,y-1,"w") then add("R2 "..x..","..y) end
    if f(x,y,"s") and f(x-1,y+1,"dse") and f(x+1,y+1,"dsw") then add("R3 "..x..","..y) end
    if f(x,y,"n") and f(x-1,y-1,"dne") and f(x+1,y-1,"dnw") then add("R3 "..x..","..y) end
    if f(x,y,"w") and f(x-1,y-1,"dsw") and f(x-1,y+1,"dnw") then add("R3 "..x..","..y) end
    if f(x,y,"e") and f(x+1,y-1,"dse") and f(x+1,y+1,"dne") then add("R3 "..x..","..y) end
    if f(x,y,"cse") and f(x,y+1,"dse") and f(x+1,y,"dse") then add("R4 "..x..","..y) end
    if f(x,y,"csw") and f(x,y+1,"dsw") and f(x-1,y,"dsw") then add("R4 "..x..","..y) end
    if f(x,y,"cne") and f(x,y-1,"dne") and f(x+1,y,"dne") then add("R4 "..x..","..y) end
    if f(x,y,"cnw") and f(x,y-1,"dnw") and f(x-1,y,"dnw") then add("R4 "..x..","..y) end
    if f(x,y,"cse") and f(x+1,y,"csw") and f(x,y+1,"cne") and f(x+1,y+1,"cnw") then add("R6 "..x..","..y) end
    for _,p in ipairs({"n","e","s","w"}) do
      if f(x,y,p) then for _,d in ipairs(B.STEPS) do
        if f(x+d[1],y+d[2],p) and f(x+2*d[1],y+2*d[2],p) then add("R5 "..p.." "..x..","..y) end
      end end
    end
    local function sV(xx,yy) return arm(xx,yy,"l") or arm(xx-1,yy,"r") end
    local function sH(xx,yy) return arm(xx,yy,"t") or arm(xx,yy-1,"b") end
    local N,S,W,E=sV(x,y-1),sV(x,y),sH(x-1,y),sH(x,y)
    if N or S or W or E then
      local q={{x-1,y-1,"cse",(N or W)},{x,y-1,"csw",(N or E)},{x-1,y,"cne",(S or W)},{x,y,"cnw",(S or E)}}
      for _,t in ipairs(q) do
        if R(t[1],t[2]) and not G(t[1],t[2]) then
          local tem=f(t[1],t[2],t[3])
          if not t[4] and not tem then add("R10 falta "..t[3].." em "..t[1]..","..t[2])
          elseif t[4] and tem then add("R10 sobra "..t[3].." em "..t[1]..","..t[2]) end
        end
      end
    end
  end end
  print("== "..setname.." "..x0..","..y0.."-"..x1..","..y1..": "..#V.." erro(s)")
  for i,v in ipairs(V) do if i<=12 then print("    "..v) end end
  return #V
end

-- ---------------------------------------------------------------- gramatica do contorno
-- Reescreve uma sequencia de beirada (um valor por linha) obedecendo a gramatica:
-- passo de +-1 so com 0 ou sinal oposto dos dois lados; par nao-nulo do mesmo sentido so se
-- os DOIS forem >=2. closeStart/closeEnd: a mancha fecha ali (o fim conta como passo grande).
-- sign: +1 se "fechar" e aumentar o valor (beirada esquerda), -1 para a direita.
function B.fixRun(orig,maxdev,noise,closeStart,closeEnd,sign)
  maxdev=maxdev or 2
  local seq,off={},0
  if closeStart then seq[1]=orig[1]-sign*3 off=1 end
  for i=1,#orig do seq[#seq+1]=orig[i] end
  if closeEnd then seq[#seq+1]=orig[#orig]+sign*3 end
  local D={-2,-1,0,1,2}
  local cur={}
  local function sk(v,s) return v.."|"..s end
  cur[sk(seq[1],9)]={cost=0,path={seq[1]}}
  for i=2,#seq do
    local nxt={}
    for k,st in pairs(cur) do
      local v,s1=k:match("([^|]+)|([^|]+)")
      v=tonumber(v) s1=tonumber(s1)
      local fixed=(closeEnd and i==#seq)
      for _,d in ipairs(D) do
        local nv=v+d
        if (fixed and nv==seq[i]) or ((not fixed) and math.abs(nv-seq[i])<=maxdev) then
          local ok=true
          if d~=0 and s1~=9 and s1~=0 and ((d>0)==(s1>0)) then
            if math.abs(d)<2 or math.abs(s1)<2 then ok=false end
          end
          if ok then
            local pen=0
            if d~=0 and s1~=9 and s1~=0 and ((d>0)~=(s1>0)) then pen=pen+4 end
            local c=st.cost+math.abs(nv-seq[i])*2+pen+(noise and math.random()*noise or 0)
            local nk=sk(nv,d)
            local e=nxt[nk]
            if not e or c<e.cost then
              local p={} for j=1,#st.path do p[j]=st.path[j] end
              p[#p+1]=nv
              nxt[nk]={cost=c,path=p}
            end
          end
        end
      end
    end
    cur=nxt
  end
  local bc,bp
  for _,st in pairs(cur) do if not bc or st.cost<bc then bc=st.cost bp=st.path end end
  if not bp then return nil end
  local out={} for i=1,#orig do out[i]=bp[i+off] end
  return out
end

print("motor de bordas instalado em _G.B")
