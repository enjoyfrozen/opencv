import unittest
import os
import sys

import cv2 as cv

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__),
                                                '..', '..')))

from stitching_detailed.megapix_scaler import MegapixScaler
from stitching_detailed.megapix_downscaler import MegapixDownscaler
#%%


class TestScaler(unittest.TestCase):

    def setUp(self):
        self.img = cv.imread("s1.jpg")
        self.size = (self.img.shape[1], self.img.shape[0])

    def test_get_scale_by_resolution(self):
        scaler = MegapixScaler(0.6)

        scale = scaler._get_scale_by_resolution(1_200_000)

        self.assertEqual(scale, 0.7071067811865476)

    def test_get_scale_by_image(self):
        scaler = MegapixScaler(0.6)

        scaler.set_scale_by_img_size(self.size)

        self.assertEqual(scaler.scale, 0.8294067854101966)

    def test_get_scaled_img_size(self):
        scaler = MegapixScaler(0.6)
        scaler.set_scale_by_img_size(self.size)

        size = scaler.get_scaled_img_size(self.size)
        self.assertEqual(size, (1033, 581))

    def test_resizing_of_scaler(self):
        scaler = MegapixScaler(0.6)
        scaler.set_scale_by_img_size(self.size)
        size = scaler.get_scaled_img_size(self.size)

        resized = scaler.resize(self.img, size)

        self.assertEqual(resized.shape, (581, 1033, 3))
        # 581*1033 = 600173 px = ~0.6 MP

    def test_get_aspect(self):
        scaler1 = MegapixScaler(1)
        scaler1._set_scale(0.1)
        scaler2 = MegapixScaler(1)
        scaler2._set_scale(0.6)

        aspect = scaler1.get_aspect_to(scaler2)

        self.assertEqual(aspect, 0.16666666666666669)

    def test_force_of_downscaling(self):
        normal_scaler = MegapixScaler(2)
        downscaler = MegapixDownscaler(2)

        normal_scaler.set_scale_by_img_size(self.size)
        downscaler.set_scale_by_img_size(self.size)

        self.assertEqual(normal_scaler.scale, 1.5142826857233715)
        self.assertEqual(downscaler.scale, 1.0)


def starttest():
    unittest.main()


if __name__ == "__main__":
    starttest()
