#!/usr/bin/env python
'''
You can download the Geometric Matching Module model from https://drive.google.com/file/d/1oBnM9R-LgH0APkwxOiEUbFqbKArh0ZcR/view?usp=sharing
You can download the Try-On Module model from https://drive.google.com/file/d/1oBnM9R-LgH0APkwxOiEUbFqbKArh0ZcR/view?usp=sharing
You can download the cloth segmentation model from https://www.dropbox.com/s/qag9vzambhhkvxr/lip_jppnet_384.pb?dl=0
You can find the OpenPose proto in opencv_extra/testdata/dnn/openpose_pose_coco.prototxt
and get .caffemodel using opencv_extra/testdata/dnn/download_models.py
'''

import argparse
import math
import numpy as np
import cv2 as cv

from numpy import linalg
from common import findFile
from human_parsing import parse_human

backends = (cv.dnn.DNN_BACKEND_DEFAULT, cv.dnn.DNN_BACKEND_INFERENCE_ENGINE, cv.dnn.DNN_BACKEND_OPENCV)
targets = (cv.dnn.DNN_TARGET_CPU, cv.dnn.DNN_TARGET_OPENCL, cv.dnn.DNN_TARGET_OPENCL_FP16, cv.dnn.DNN_TARGET_MYRIAD)

parser = argparse.ArgumentParser(description='Use this script to run virtial try-on using CP-VTON',
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--input_image', '-i', help='Path to image with person. Skip this argument to capture frames from a camera.')
parser.add_argument('--input_cloth', '-c', required=True, help='Path to target cloth image')
parser.add_argument('--gmm_model', '-gmm', default='gmm.onnx', help='Path to Geometric Matching Module .onnx model.')
parser.add_argument('--tom_model', '-tom', default='tom.onnx', help='Path to Try-On Module .onnx model.')
parser.add_argument('--segmentation_model', default='lip_jppnet_384.pb', help='Path to cloth segmentation .pb model.')
parser.add_argument('--openpose_proto', required=True, help='Path to OpenPose .prototxt model was trained on COCO dataset.')
parser.add_argument('--openpose_model', required=True, help='Path to OpenPose .caffemodel model was trained on COCO dataset.')
parser.add_argument('--backend', choices=backends, default=cv.dnn.DNN_BACKEND_DEFAULT, type=int,
                    help="Choose one of computation backends: "
                            "%d: automatically (by default), "
                            "%d: Intel's Deep Learning Inference Engine (https://software.intel.com/openvino-toolkit), "
                            "%d: OpenCV implementation" % backends)
parser.add_argument('--target', choices=targets, default=cv.dnn.DNN_TARGET_CPU, type=int,
                    help='Choose one of target computation devices: '
                            '%d: CPU target (by default), '
                            '%d: OpenCL, '
                            '%d: OpenCL fp16 (half-float precision), '
                            '%d: VPU' % targets)
args, _ = parser.parse_known_args()


def get_pose_map(image_path, proto_path, model_path, backend, target, height=256, width=192):
    radius = 5
    img = cv.imread(image_path)
    inp = cv.dnn.blobFromImage(img, 1.0 / 255, (width, height))

    net = cv.dnn.readNet(proto_path, model_path)
    net.setPreferableBackend(backend)
    net.setPreferableTarget(target)
    net.setInput(inp)
    out = net.forward()

    threshold = 0.1
    pose_map = np.zeros((height, width, out.shape[1] - 1))
    # last label: Background
    for i in range(0, out.shape[1] - 1):
        heatMap = out[0, i, :, :]
        keypoint = np.zeros((height, width, 1))
        _, conf, _, point = cv.minMaxLoc(heatMap)
        x = (width * point[0]) / out.shape[3]
        y = (height * point[1]) / out.shape[2]
        x, y = int(x), int(y)
        if conf > threshold and x > 0 and y > 0:
            cv.rectangle(keypoint, (x - radius, y - radius), (x + radius, y + radius), (255, 255, 255), cv.FILLED)
        keypoint[:, :, 0] = (keypoint[:, :, 0] - 127.5) / 127.5
        pose_map[:, :, i] = keypoint.reshape(height, width)

    pose_map = pose_map.transpose(2, 0, 1)
    return pose_map


class BilinearFilter(object):
    def precompute_coeffs(self, inSize, outSize):
        filterscale = max(1.0, inSize / outSize)
        ksize = math.ceil(filterscale) * 2 + 1

        kk = np.zeros(shape=(outSize * ksize, ), dtype=np.float32)
        bounds = np.empty(shape=(outSize * 2, ), dtype=np.int32)

        centers = (np.arange(outSize) + 0.5) * filterscale + 0.5
        bounds[::2] = np.where(centers - filterscale < 0, 0, centers - filterscale)
        bounds[1::2] = np.where(centers + filterscale > inSize, inSize, centers + filterscale) - bounds[::2]
        xmins = bounds[::2] - centers + 1

        points = np.array([np.arange(row) + xmins[i] for i, row in enumerate(bounds[1::2])]) / filterscale
        for xx in range(0, outSize):
            point = points[xx]
            bilinear = np.where(point < 1.0, 1.0 - abs(point), 0.0)
            ww = np.sum(bilinear)
            kk[xx * ksize : xx * ksize + bilinear.size] = np.where(ww == 0.0, bilinear, bilinear / ww)
        return bounds, kk, ksize

    def ResampleHorizontal(self, out, img, ksize, bounds, kk):
        for yy in range(0, out.shape[0]):
            for xx in range(0, out.shape[1]):
                xmin = bounds[xx * 2 + 0]
                xmax = bounds[xx * 2 + 1]
                k = kk[xx * ksize : xx * ksize + xmax]
                out[yy, xx] = np.round(np.sum(img[yy, xmin : xmin + xmax] * k))

    def ResampleVertical(self, out, img, ksize, bounds, kk):
        for yy in range(0, out.shape[0]):
            ymin = bounds[yy * 2 + 0]
            ymax = bounds[yy * 2 + 1]
            k = kk[yy * ksize: yy * ksize + ymax]
            out[yy] = np.round(np.sum(img[ymin : ymin + ymax, 0:out.shape[1]] * k[:, np.newaxis], axis=0))

    def ImagingResample(self, img, xsize, ysize):
        height, width, *args = img.shape
        bounds_horiz, kk_horiz, ksize_horiz = self.precompute_coeffs(width, xsize)
        bounds_vert, kk_vert, ksize_vert    = self.precompute_coeffs(height, ysize)

        out_hor = np.empty((img.shape[0], xsize), dtype=np.uint8)
        self.ResampleHorizontal(out_hor, img, ksize_horiz, bounds_horiz, kk_horiz)
        out = np.empty((ysize, xsize), dtype=np.uint8)
        self.ResampleVertical(out, out_hor, ksize_vert, bounds_vert, kk_vert)
        return out


def prepare_agnostic(segm_image, image_name, pose_map, height=256, width=192):
    palette = {
        'Background'   : (0, 0, 0),
        'Hat'          : (128, 0, 0),
        'Hair'         : (255, 0, 0),
        'Glove'        : (0, 85, 0),
        'Sunglasses'   : (170, 0, 51),
        'UpperClothes' : (255, 85, 0),
        'Dress'        : (0, 0, 85),
        'Coat'         : (0, 119, 221),
        'Socks'        : (85, 85, 0),
        'Pants'        : (0, 85, 85),
        'Jumpsuits'    : (85, 51, 0),
        'Scarf'        : (52, 86, 128),
        'Skirt'        : (0, 128, 0),
        'Face'         : (0, 0, 255),
        'Left-arm'     : (51, 170, 221),
        'Right-arm'    : (0, 255, 255),
        'Left-leg'     : (85, 255, 170),
        'Right-leg'    : (170, 255, 85),
        'Left-shoe'    : (255, 255, 0),
        'Right-shoe'   : (255, 170, 0)
    }
    color2label = {val: key for key, val in palette.items()}
    head_labels = ['Hat', 'Hair', 'Sunglasses', 'Face', 'Pants', 'Skirt']

    segm_image = cv.cvtColor(segm_image, cv.COLOR_BGR2RGB)
    phead = np.zeros((1, height, width), dtype=np.float32)
    pose_shape = np.zeros((height, width), dtype=np.uint8)
    for r in range(height):
        for c in range(width):
            pixel = tuple(segm_image[r, c])
            if tuple(pixel) in color2label:
                if color2label[pixel] in head_labels:
                    phead[0, r, c] = 1
                if color2label[pixel] != 'Background':
                    pose_shape[r, c] = 255

    input_image = cv.imread(image_name)
    input_image = cv.resize(input_image, (width, height), cv.INTER_LINEAR)
    input_image = cv.cvtColor(input_image, cv.COLOR_BGR2RGB)
    input_image = (input_image - 127.5) / 127.5
    input_image = input_image.transpose(2, 0, 1)

    img_head = input_image * phead - (1 - phead)

    downsample = BilinearFilter()
    down = downsample.ImagingResample(pose_shape, width // 16, height // 16)
    res_shape = cv.resize(down, (width, height), cv.INTER_LINEAR)

    res_shape = cv.dnn.blobFromImage(res_shape, 1.0 / 127.5, mean=(127.5, 127.5, 127.5), swapRB=True)
    res_shape = res_shape.squeeze(0)

    agnostic = np.concatenate((res_shape, img_head, pose_map), axis=0)
    agnostic = np.expand_dims(agnostic, axis=0)
    return agnostic


def get_warped_cloth(cloth_path, agnostic, model, backend, target, height=256, width=192):
    cloth_img = cv.imread(cloth_path)
    cloth = cv.dnn.blobFromImage(cloth_img, 1.0 / 127.5, (width, height), mean=(127.5, 127.5, 127.5), swapRB=True)

    theta = run_gmm(agnostic, cloth, model, backend, target)
    grid = postprocess(theta)
    warped_cloth = bilinear_sampler(cloth, grid).astype(np.float32)
    return warped_cloth


def run_gmm(agnostic, c, model, backend, target):
    class CorrelationLayer(object):
        def __init__(self, params, blobs):
            super(CorrelationLayer, self).__init__()

        def getMemoryShapes(self, inputs):
            fetureAShape = inputs[0]
            b, c, h, w = fetureAShape
            return [[b, h * w, h, w]]

        def forward(self, inputs):
            feature_A, feature_B = inputs
            b, c, h, w = feature_A.shape
            feature_A = feature_A.transpose(0, 1, 3, 2)
            feature_A = np.reshape(feature_A, (b, c, h * w))
            feature_B = np.reshape(feature_B, (b, c, h * w))
            feature_B = feature_B.transpose(0, 2, 1)
            feature_mul = feature_B @ feature_A
            feature_mul= np.reshape(feature_mul, (b, h, w, h * w))
            feature_mul = feature_mul.transpose(0, 1, 3, 2)
            correlation_tensor = feature_mul.transpose(0, 2, 1, 3)
            correlation_tensor = np.ascontiguousarray(correlation_tensor)
            return [correlation_tensor]

    cv.dnn_registerLayer('Correlation', CorrelationLayer)

    net = cv.dnn.readNet(model)
    net.setPreferableBackend(backend)
    net.setPreferableTarget(target)
    net.setInput(agnostic, "input.1")
    net.setInput(c, "input.18")
    theta = net.forward()

    cv.dnn_unregisterLayer('Correlation')
    return theta


def get_tryon(agnostic, warp_cloth, model, backend, target):
    inp = np.concatenate([agnostic, warp_cloth], axis=1)

    net = cv.dnn.readNet(model)
    net.setPreferableBackend(backend)
    net.setPreferableTarget(target)
    net.setInput(inp)
    out = net.forward()

    p_rendered, m_composite = np.split(out, [3], axis=1)
    p_rendered = np.tanh(p_rendered)
    m_composite = 1 / (1 + np.exp(-m_composite))

    p_tryon = warp_cloth * m_composite + p_rendered * (1 - m_composite)
    rgb_p_tryon = cv.cvtColor(p_tryon.squeeze(0).transpose(1, 2, 0), cv.COLOR_BGR2RGB)
    rgb_p_tryon = (rgb_p_tryon + 1) / 2
    return rgb_p_tryon


def compute_L_inverse(X, Y):
    N = X.shape[0]

    Xmat = np.tile(X, (1, N))
    Ymat = np.tile(Y, (1, N))
    P_dist_squared = np.power(Xmat - Xmat.transpose(1, 0), 2) + np.power(Ymat - Ymat.transpose(1, 0), 2)

    P_dist_squared[P_dist_squared == 0] = 1
    K = np.multiply(P_dist_squared, np.log(P_dist_squared))

    O = np.ones([N, 1], dtype=np.float32)
    Z = np.zeros([3, 3], dtype=np.float32)
    P = np.concatenate([O, X, Y], axis=1)
    first = np.concatenate((K, P), axis=1)
    second = np.concatenate((P.transpose(1, 0), Z), axis=1)
    L = np.concatenate((first, second), axis=0)
    Li = linalg.inv(L)
    return Li

def prepare_to_transform(out_h=256, out_w=192, grid_size=5):
    grid = np.zeros([out_h, out_w, 3], dtype=np.float32)
    grid_X, grid_Y = np.meshgrid(np.linspace(-1, 1, out_w), np.linspace(-1, 1, out_h))
    grid_X = np.expand_dims(np.expand_dims(grid_X, axis=0), axis=3)
    grid_Y = np.expand_dims(np.expand_dims(grid_Y, axis=0), axis=3)

    axis_coords = np.linspace(-1, 1, grid_size)
    N = grid_size ** 2
    P_Y, P_X = np.meshgrid(axis_coords, axis_coords)

    P_X = np.reshape(P_X,(-1, 1))
    P_Y = np.reshape(P_Y,(-1, 1))

    P_X = np.expand_dims(np.expand_dims(np.expand_dims(P_X, axis=2), axis=3), axis=4).transpose(4, 1, 2, 3, 0)
    P_Y = np.expand_dims(np.expand_dims(np.expand_dims(P_Y, axis=2), axis=3), axis=4).transpose(4, 1, 2, 3, 0)
    return grid_X, grid_Y, N, P_X, P_Y

def expand_torch(X, shape):
    if len(X.shape) != len(shape):
        return X.flatten().reshape(shape)
    else:
        axis = [1 if src == dst else dst for src, dst in zip(X.shape, shape)]
        return np.tile(X, axis)

def apply_transformation(theta, points, N, P_X, P_Y):
    if len(theta.shape) == 2:
        theta = np.expand_dims(np.expand_dims(theta, axis=2), axis=3)

    batch_size = theta.shape[0]

    P_X_base = np.copy(P_X)
    P_Y_base = np.copy(P_Y)

    Li = compute_L_inverse(np.reshape(P_X, (N, -1)), np.reshape(P_Y, (N, -1)))
    Li = np.expand_dims(Li, axis=0)

    # split theta into point coordinates
    Q_X = np.squeeze(theta[:, :N, :, :], axis=3)
    Q_Y = np.squeeze(theta[:, N:, :, :], axis=3)

    Q_X += expand_torch(P_X_base, Q_X.shape)
    Q_Y += expand_torch(P_Y_base, Q_Y.shape)

    points_b = points.shape[0]
    points_h = points.shape[1]
    points_w = points.shape[2]

    P_X = expand_torch(P_X, (1, points_h, points_w, 1, N))
    P_Y = expand_torch(P_Y, (1, points_h, points_w, 1, N))

    W_X = expand_torch(Li[:,:N,:N], (batch_size, N, N)) @ Q_X
    W_Y = expand_torch(Li[:,:N,:N], (batch_size, N, N)) @ Q_Y

    W_X = np.expand_dims(np.expand_dims(W_X, axis=3), axis=4).transpose(0, 4, 2, 3, 1)
    W_X = np.repeat(W_X, points_h, axis=1)
    W_X = np.repeat(W_X, points_w, axis=2)

    W_Y = np.expand_dims(np.expand_dims(W_Y, axis=3), axis=4).transpose(0, 4, 2, 3, 1)
    W_Y = np.repeat(W_Y, points_h, axis=1)
    W_Y = np.repeat(W_Y, points_w, axis=2)

    A_X = expand_torch(Li[:, N:, :N], (batch_size, 3, N)) @ Q_X
    A_Y = expand_torch(Li[:, N:, :N], (batch_size, 3, N)) @ Q_Y

    A_X = np.expand_dims(np.expand_dims(A_X, axis=3), axis=4).transpose(0, 4, 2, 3, 1)
    A_X = np.repeat(A_X, points_h, axis=1)
    A_X = np.repeat(A_X, points_w, axis=2)

    A_Y = np.expand_dims(np.expand_dims(A_Y, axis=3), axis=4).transpose(0, 4, 2, 3, 1)
    A_Y = np.repeat(A_Y, points_h, axis=1)
    A_Y = np.repeat(A_Y, points_w, axis=2)

    points_X_for_summation = np.expand_dims(np.expand_dims(points[:, :, :, 0], axis=3), axis=4)
    points_X_for_summation = expand_torch(points_X_for_summation, points[:, :, :, 0].shape + (1, N))

    points_Y_for_summation = np.expand_dims(np.expand_dims(points[:, :, :, 1], axis=3), axis=4)
    points_Y_for_summation = expand_torch(points_Y_for_summation, points[:, :, :, 0].shape + (1, N))

    if points_b == 1:
        delta_X = points_X_for_summation - P_X
        delta_Y = points_Y_for_summation - P_Y
    else:
        delta_X = points_X_for_summation - expand_torch(P_X, points_X_for_summation.shape)
        delta_Y = points_Y_for_summation - expand_torch(P_Y, points_Y_for_summation.shape)

    dist_squared = np.power(delta_X, 2) + np.power(delta_Y, 2)
    dist_squared[dist_squared == 0] = 1
    U = np.multiply(dist_squared, np.log(dist_squared))

    points_X_batch = np.expand_dims(points[:,:,:,0], axis=3)
    points_Y_batch = np.expand_dims(points[:,:,:,1], axis=3)

    if points_b == 1:
        points_X_batch = expand_torch(points_X_batch, (batch_size, ) + points_X_batch.shape[1:])
        points_Y_batch = expand_torch(points_Y_batch, (batch_size, ) + points_Y_batch.shape[1:])

    points_X_prime = A_X[:,:,:,:,0]+ \
                    np.multiply(A_X[:,:,:,:,1], points_X_batch) + \
                    np.multiply(A_X[:,:,:,:,2], points_Y_batch) + \
                    np.sum(np.multiply(W_X, expand_torch(U, W_X.shape)), 4)

    points_Y_prime = A_Y[:,:,:,:,0]+ \
                    np.multiply(A_Y[:,:,:,:,1], points_X_batch) + \
                    np.multiply(A_Y[:,:,:,:,2], points_Y_batch) + \
                    np.sum(np.multiply(W_Y, expand_torch(U, W_Y.shape)), 4)

    return np.concatenate((points_X_prime, points_Y_prime), 3)

def postprocess(theta):
    grid_X, grid_Y, N, P_X, P_Y = prepare_to_transform()
    warped_grid = apply_transformation(theta, np.concatenate((grid_X, grid_Y), axis=3), N, P_X, P_Y)
    return warped_grid

def bilinear_sampler(img, grid):
    x, y = grid[:,:,:,0], grid[:,:,:,1]

    H = img.shape[2]
    W = img.shape[3]
    max_y = H - 1
    max_x = W - 1

    # rescale x and y to [0, W-1/H-1]
    x = 0.5 * (x + 1.0) * (max_x - 1)
    y = 0.5 * (y + 1.0) * (max_y - 1)

    # grab 4 nearest corner points for each (x_i, y_i)
    x0 = np.floor(x).astype(int)
    x1 = x0 + 1
    y0 = np.floor(y).astype(int)
    y1 = y0 + 1

    # calculate deltas
    wa = (x1 - x) * (y1 - y)
    wb = (x1 - x) * (y  - y0)
    wc = (x - x0) * (y1 - y)
    wd = (x - x0) * (y  - y0)

    # clip to range [0, H-1/W-1] to not violate img boundaries
    x0 = np.clip(x0, 0, max_x)
    x1 = np.clip(x1, 0, max_x)
    y0 = np.clip(y0, 0, max_y)
    y1 = np.clip(y1, 0, max_y)

    # get pixel value at corner coords
    img = img.reshape(-1, H, W)
    Ia = img[:, y0, x0].swapaxes(0, 1)
    Ib = img[:, y1, x0].swapaxes(0, 1)
    Ic = img[:, y0, x1].swapaxes(0, 1)
    Id = img[:, y1, x1].swapaxes(0, 1)

    wa = np.expand_dims(wa, axis=0)
    wb = np.expand_dims(wb, axis=0)
    wc = np.expand_dims(wc, axis=0)
    wd = np.expand_dims(wd, axis=0)

    # compute output
    out = wa*Ia + wb*Ib + wc*Ic + wd*Id
    return out


if __name__ == "__main__":
    pose = get_pose_map(args.input_image, findFile(args.openpose_proto),
                        findFile(args.openpose_model), args.backend, args.target)
    segm_image = parse_human(args.input_image, args.segmentation_model)
    segm_image = cv.resize(segm_image, (192, 256), cv.INTER_LINEAR)

    agnostic = prepare_agnostic(segm_image, args.input_image, pose)
    warped_cloth = get_warped_cloth(args.input_cloth, agnostic, args.gmm_model, args.backend, args.target)
    output = get_tryon(agnostic, warped_cloth, args.tom_model, args.backend, args.target)

    winName = 'Virtual Try-On'
    cv.namedWindow(winName, cv.WINDOW_AUTOSIZE)
    cv.imshow(winName, output)
    cv.waitKey()
