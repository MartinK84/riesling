import os
import sys
import inspect
import riesling
import unittest

# define test order to make reusing of previous results possible
order = ['test_image', 'test_sense', 'test_kspace', 'test_rss', 'test_nufft']
def compare(a, b):
    if order.index(a) < order.index(b):
        return -1
    elif order.index(a) < order.index(b):
        return 1
    else:
        return 0
unittest.defaultTestLoader.sortTestMethodsUsing = compare

# helper class to plot results if run with -vv
def plot(data, dims, sel, title=''):
    if '-vv' in sys.argv:
        import numpy as np
        import matplotlib.pyplot as plt

        data = data.copy()
        data = np.abs(data)
        data = data.isel(sel) # select subset
        if data.ndim > len(dims): # transpose displayed dimensions to beginning
            data = data.transpose(*dims, ...)
        else:                
            data = data.transpose(*dims)
        if data.ndim > 2: # linearize all other dimensions
            lin_dims = list(reversed(data.dims[1:]))
            data = data.stack({f"{str(lin_dims)}": lin_dims})
        
        fig, ax = plt.subplots(nrows=1, ncols=1)
        ax.imshow(data)
        ax.set_title(title)
        ax.set_xlabel(data.dims[1])
        ax.set_ylabel(data.dims[0])
        plt.show()

class TestRead(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.prefix = 'unittest'
        cls.matrix=64
        cls.voxsz=3
        cls.channels=4
    
    @classmethod
    def tearDownClass(cls):
        os.system(f'rm {cls.prefix}*.h5')

    def test_image(self):
        os.system(f'riesling phantom {self.prefix} --matrix={self.matrix} --vox-size={self.voxsz} --nex=0.5 --gradcubes --size=64')
        data = riesling.data.read(f'{self.prefix}.h5')
        self.assertEqual(data.shape, (1, 64, 64, 64, 1))
        self.assertEqual(data.dims, ('volume', 'z', 'y', 'x', 'image'))
        plot(data, ['x','y'], {'volume':0, 'image':0, 'z':[16,32,48]}, title='phantom')

    def test_sense(self):
        os.system(f'riesling sense-sim {self.prefix}-sim --matrix={self.matrix} --vox-size={self.voxsz} --channels={self.channels}')
        data = riesling.data.read(f'{self.prefix}-sim-sense.h5')
        self.assertEqual(data.shape, (64, 64, 64, 4))
        self.assertEqual(data.dims, ('z', 'y', 'x', 'channel'))
        plot(data, ['x','y'], {'z':32}, title='sim-sense')

    def test_kspace(self):
        os.system(f'riesling recon --fwd --sense={self.prefix}-sim-sense.h5 --sense-fov=-1 {self.prefix}.h5')
        data = riesling.data.read(f'{self.prefix}-recon.h5')
        self.assertEqual(data.shape, (1, 1, 2048, 64, 4))
        self.assertEqual(data.dims, ('volume', 'slab', 'trace', 'sample', 'channel'))
        self.assertEqual(data.attrs['trajectory'].shape, (2048, 64, 3))
        plot(data, ['sample', 'trace'], {'volume':0, 'slab':0, 'channel':0, 'trace': range(0, 256)}, title='recon')

    def test_rss(self):
        os.system(f'riesling rss {self.prefix}-recon.h5')
        data = riesling.data.read(f'{self.prefix}-recon-rss.h5')
        self.assertEqual(data.shape, (1, 64, 64, 64, 1))
        self.assertEqual(data.dims, ('volume', 'z', 'y', 'x', 'image'))
        plot(data, ['x','y'], {'volume':0, 'image':0, 'z':[16,32,48]}, title='rss')

    def test_nufft(self):
        os.system(f'riesling nufft {self.prefix}-recon.h5 --out={self.prefix}')
        data = riesling.data.read(f'{self.prefix}-recon-nufft.h5')
        self.assertEqual(data.shape, (1, 64, 64, 64, 1, 4))
        self.assertEqual(data.dims, ('volume', 'z', 'y', 'x', 'image', 'channel'))
        plot(data, ['x','y'], {'volume':0, 'image':0, 'z':[16,32,48], 'channel':0}, title='nufft - channel=0')

if __name__ == "__main__":
    unittest.main()