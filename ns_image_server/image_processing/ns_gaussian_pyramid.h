#pragma once
#include "ipp.h"
#ifdef _MSC_VER
#define finline                     __forceinline
#else
#define finline						__attribute__((always_inline))
#endif

template<class T>
void ns_ippi_safe_delete(T * & pointer) {
	if (pointer == 0) return;
	T * tmp(pointer);
	pointer = 0;
	ippiFree(tmp);
}
template<class T>
void ns_ipps_safe_delete(T * & pointer) {
	if (pointer == 0) return;
	T * tmp(pointer);
	pointer = 0;
	ippsFree(tmp);
}


//#define NS_DEBUG_IMAGE_ACCESS
class ns_intel_image_32f {
public:
	ns_intel_image_32f() :buffer(0) {}
	~ns_intel_image_32f() {
		clear();
	}
	finline Ipp32f &  val(const int & y, const int & x) { return buffer[y*line_step_in_pixels + x]; }
	finline const Ipp32f & val(const int & y, const int & x) const { return buffer[y*line_step_in_pixels + x]; }

	const Ipp32f
		//xxx
#ifndef NS_DEBUG_IMAGE_ACCESS
		finline
#endif
		sample_d(const float y, const float x) const {

		const int p0x(xs_float::xs_FloorToInt(x)),
			p0y(xs_float::xs_FloorToInt(y));
		const int p1x(p0x + 1),
			p1y(p0y + 1);
		const float dx(x - (float)p0x),
			dy(y - (float)p0y);
		const float d1x(1.0 - dx),
			d1y(1.0 - dy);

#ifdef NS_DEBUG_IMAGE_ACCESS
		if (p0y < 0 || p1y < 0 ||
			p0x < 0 || p1x < 0 ||
			p0y >= properties_.height || p1y >= properties_.height ||
			p0x >= properties_.width || p1x >= properties_.width) {
			cerr << "ns_intel_image_32f::Out of bound access: (" << x << "," << y << ") [" << properties_.width << "," << properties_.height << "]: (" << p0x << "," << p0y << ")-(" << p1x << "," << p1y << ")\n";
			throw ns_ex("ns_intel_image_32f::Out of bound access: (") << x << "," << y << "): (" << p0x << "," << p0y << ")-(" << p1x << "," << p1y << ")";
		}
#endif
		//note that accessing val[height-1][width-1]  only works because we allocate an extra 1 pixel right and bottom buffer
		//(see the function init()
		return	val(p0y, p0x) * (d1y)*(d1x)+
			val(p0y, p1x) * (d1y)*(dx)+
			val(p1y, p0x) * (dy)*(d1x)+
			val(p1y, p1x) * (dy)*(dx);
	}


	int  init(const ns_image_properties & prop) {
		return init(prop.width, prop.height);
	}
	void clear() {
		properties_.width = 0;
		properties_.height = 0;
		line_step_in_bytes = 0;
		line_step_in_pixels = 0;
		ns_ippi_safe_delete(buffer);
	}
	int init(const int width, const int height) {
		if (properties_.width == width && properties_.height == height)
			return line_step_in_bytes;

		clear();
		//SUBTLE THING: We add a one pixel righthand boundary around images
		//to allow subsampling to handle boundaries correctly.
		//calling sample_d[height-1][width-1] will produce the code
		// sample_d[height-1][width-1]*1 + 0*sample_d[height-1][width]+0*sample_d[height][width-1].
		// by allocating that 1 pixel boundary, we prevent accessing unallocated memory
		// while also we never propigating the uninitialized values at sample_d[height][..] or sample_d[..][width]
		// which are multiplied by zero in every case.
		buffer = ippiMalloc_32f_C1(width + 1, height + 1, &line_step_in_bytes);
		if (buffer == NULL) {
			//ippiFree(buffer);
			throw ns_ex("ns_intel_image_32f::Could not allocate buffer ") << width << "," << height;
		}
		line_step_in_pixels = line_step_in_bytes / sizeof(Ipp32f);
		properties_.width = width;
		properties_.height = height;
		properties_.components = 1;
		return line_step_in_bytes;
	}
	void convert(ns_image_whole<float> & im) {
		im.init(properties_);
		for (unsigned int y = 0; y < properties_.height; y++)
			for (unsigned int x = 0; x < properties_.height; x++)
				im[y][x] = val(y, x);
	}
	const ns_image_properties & properties() const {
		return properties_;
	}
	Ipp32f  * buffer;
	int line_step_in_bytes;
	int line_step_in_pixels;
private:
	ns_image_properties properties_;
};


class ns_gaussian_pyramid {
public:
	ns_gaussian_pyramid() :image_size(0, 0, 0),
		pyrLStateSize(0), pyrLBufferSize(0), max_pyrLStateSize(0), max_pyrLBufferSize(0), pPyrLStateBuf(0), pPyrLBuffer(0),
		pPyrStruct(0), pPyrBuffer(0), pPyrStrBuffer(0), max_pyrBufferSize(0), max_pyrStructSize(0), pyrBufferSize(0), pyrStructSize(0) {}
	void clear() {
		for (unsigned int i = 0; i < ns_calc_best_alignment_fast::ns_max_pyramid_size; i++)
			image_scaled[i].clear();
		image_size.width = image_size.height = 0;
		ns_ipps_safe_delete(pPyrLStateBuf);
		ns_ipps_safe_delete(pPyrLBuffer);
		ns_ipps_safe_delete(pPyrStrBuffer);
		ns_ipps_safe_delete(pPyrBuffer);
	}
	~ns_gaussian_pyramid() { clear(); }

	template<class T>
	void calculate(const ns_image_whole<T> & im) {
		ns_image_properties p(im.properties());
		allocate(p);

		//copy over raw image to first layer of pyramid
		for (unsigned int y = 0; y < p.height; y++)
			for (unsigned int x = 0; x < p.width; x++)
				image_scaled[0].val(y, x) = im[y][x] / 255.f;

		/* Perform downsampling of the image with 5x5 Gaussian kernel */
		for (int i = 1; i < num_current_pyramid_levels; i++) {
			int status = ippiPyramidLayerDown_32f_C1R(image_scaled[i - 1].buffer, pPyrStruct->pStep[i - 1], pPyrStruct->pRoi[i - 1],
				image_scaled[i].buffer, pPyrStruct->pStep[i], pPyrStruct->pRoi[i], (IppiPyramidDownState_32f_C1R*)pPyrStruct->pState);
			if (status != ippStsNoErr)
				throw ns_ex("Could not calculate pyramid layer ") << i;
		}
	}


	//contains some temporary image data buffers that are kept to avoid reallocating memory each time
	ns_intel_image_32f image_scaled[ns_calc_best_alignment_fast::ns_max_pyramid_size];
	ns_image_properties image_size;
	int num_current_pyramid_levels;


private:

	void allocate(const ns_image_properties & image) {

		long min_d(image.width < image.height ? image.width : image.height);
		//if we go resample to images smaller than 16x16 pixels, our gradient calculations
		//we lose the important features we want to align.
		num_current_pyramid_levels = log2(min_d) - 4;

		/* Computes the temporary work buffer size */
		IppiSize    roiSize = { image.width,image.height };
		float rate(2);
		const int kernel_size = 5;
		Ipp32f kernel[kernel_size] = { 1.f, 4.f, 6.f,4.f,1.f };

		int status = ippiPyramidGetSize(&pyrStructSize, &pyrBufferSize, num_current_pyramid_levels - 1, roiSize, rate);
		if (status != ippStsNoErr)
			throw ns_ex("Could not estimate intel pyramid size");
		if (pyrStructSize > max_pyrStructSize) {
			max_pyrStructSize = pyrStructSize;
			ns_ipps_safe_delete(pPyrStrBuffer);
			pPyrStrBuffer = ippsMalloc_8u(pyrStructSize);
		}
		if (pyrBufferSize > max_pyrBufferSize) {
			max_pyrBufferSize = pyrBufferSize;
			ns_ipps_safe_delete(pPyrBuffer);
			pPyrBuffer = ippsMalloc_8u(pyrBufferSize);
		}
		/* Initializes Gaussian structure for pyramids */
		status = ippiPyramidInit(&pPyrStruct, num_current_pyramid_levels - 1, roiSize, rate, pPyrStrBuffer, pPyrBuffer);
		if (status != ippStsNoErr)
			throw ns_ex("Could not make intel pyramid");
		/* Correct maximum scale level */
		/* Allocate structures to calculate pyramid layers */
		status = ippiPyramidLayerDownGetSize_32f_C1R(roiSize, rate, kernel_size, &pyrLStateSize, &pyrLBufferSize);
		if (status != ippStsNoErr)
			throw ns_ex("Could not estimate pyramid layer down size");
		if (pyrLStateSize > max_pyrLStateSize) {
			max_pyrLStateSize = pyrLStateSize;
			ns_ipps_safe_delete(pPyrLStateBuf);
			pPyrLStateBuf = ippsMalloc_8u(pyrLStateSize);
		}
		if (pyrLBufferSize > max_pyrLBufferSize) {
			max_pyrLBufferSize = pyrLBufferSize;
			ns_ipps_safe_delete(pPyrLBuffer);
			pPyrLBuffer = ippsMalloc_8u(pyrLBufferSize);
		}
		/* Initialize the structure for creating a lower pyramid layer */
		status = ippiPyramidLayerDownInit_32f_C1R((IppiPyramidDownState_32f_C1R**)&pPyrStruct->pState, roiSize, rate,
			kernel, kernel_size, IPPI_INTER_LINEAR, pPyrLStateBuf, pPyrLBuffer);
		if (status != ippStsNoErr)
			throw ns_ex("Could not init pyramid layers");
		/* Allocate pyramid layers */

		image_size = image;
		for (int i = 0; i < num_current_pyramid_levels; i++) {
			if (image_scaled[i].properties().width != pPyrStruct->pRoi[i].width ||
				image_scaled[i].properties().height != pPyrStruct->pRoi[i].height)
				pPyrStruct->pStep[i] = image_scaled[i].init(pPyrStruct->pRoi[i].width, pPyrStruct->pRoi[i].height);
			else
				pPyrStruct->pStep[i] = image_scaled[i].line_step_in_bytes;
		}
		for (int i = num_current_pyramid_levels; i < ns_calc_best_alignment_fast::ns_max_pyramid_size; i++)
			image_scaled[i].clear();
	}

	//intel specific pyramid info
	int pyrBufferSize, max_pyrBufferSize,
		pyrStructSize, max_pyrStructSize;
	IppiPyramid *pPyrStruct;
	Ipp8u       *pPyrBuffer;
	Ipp8u       *pPyrStrBuffer;
	int      pyrLStateSize, max_pyrLStateSize;
	int      pyrLBufferSize, max_pyrLBufferSize;
	Ipp8u   *pPyrLStateBuf;
	Ipp8u   *pPyrLBuffer;
};