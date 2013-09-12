import sys
import numpy as np
import matplotlib.pyplot as plt

import scipy
from collections import Counter

lines = file(sys.argv[1], "rt").readlines()
s = map(lambda x: int(x.strip("\n")), lines)

avg = np.mean(s)
cnt = len(s)

print "%s avg=%d(us) cnt=%d" % (sys.argv[1], avg, cnt)

plt.hist(s, bins=60)
plt.show()
