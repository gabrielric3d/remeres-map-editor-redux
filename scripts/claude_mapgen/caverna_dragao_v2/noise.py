import math, random
class ValueNoise:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.p = list(range(256)); self.r.shuffle(self.p); self.p += self.p
        self.v = [self.r.uniform(-1, 1) for _ in range(256)]
    def _h(self, x, y):
        return self.v[self.p[(self.p[x & 255] + y) & 255]]
    def at(self, x, y):
        xi, yi = math.floor(x), math.floor(y)
        fx, fy = x - xi, y - yi
        sx, sy = fx*fx*(3-2*fx), fy*fy*(3-2*fy)
        a = self._h(xi, yi); b = self._h(xi+1, yi); c = self._h(xi, yi+1); d = self._h(xi+1, yi+1)
        return (a*(1-sx) + b*sx)*(1-sy) + (c*(1-sx) + d*sx)*sy
    def fbm(self, x, y, scale, oct=3, pers=0.5):
        s, amp, tot, norm = 1.0/scale, 1.0, 0.0, 0.0
        for o in range(oct):
            tot += amp * self.at(x*s + o*17.3, y*s + o*31.7)
            norm += amp; amp *= pers; s *= 2
        return tot / norm
