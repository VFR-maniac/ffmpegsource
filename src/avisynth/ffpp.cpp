//  Copyright (c) 2007-2011 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "ffpp.h"
#include "avsutils.h"
#include "../core/videoutils.h"

#ifdef FFMS_USE_POSTPROC

static int64_t AvisynthToSWSCPUFlags(long AvisynthFlags) {
	int64_t Flags = 0;
#ifdef SWS_CPU_CAPS_MMX
	if (AvisynthFlags & CPUF_MMX)
		Flags |= SWS_CPU_CAPS_MMX;
	if (AvisynthFlags & CPUF_INTEGER_SSE)
		Flags |= SWS_CPU_CAPS_MMX2;
	if (AvisynthFlags & CPUF_3DNOW_EXT)
		Flags |= SWS_CPU_CAPS_3DNOW;
	if (AvisynthFlags & CPUF_SSE2)
		Flags |= SWS_CPU_CAPS_SSE2;
#endif
	return Flags;
}

FFPP::FFPP(PClip AChild, const char *PP, IScriptEnvironment *Env) : GenericVideoFilter(AChild) {
	if (!strcmp(PP, ""))
		Env->ThrowError("FFPP: PP argument is empty");

	PPContext = NULL;
	PPMode = NULL;
	SWSTo422P = NULL;
	SWSFrom422P = NULL;

	memset(&InputPicture, 0, sizeof(InputPicture));
	memset(&OutputPicture, 0, sizeof(OutputPicture));

	// due to a parsing bug in libpostproc it can read beyond the end of a string
	// adding a ',' prevents the bug from manifesting
	// libav head 2011-08-26
	std::string s = PP;
	s.append(",");
	PPMode = pp_get_mode_by_name_and_quality(s.c_str(), PP_QUALITY_MAX);
	if (!PPMode)
		Env->ThrowError("FFPP: Invalid postprocesing settings");
	
	int64_t Flags = AvisynthToSWSCPUFlags(Env->GetCPUFlags());

	if (vi.IsYV12()) {
		Flags |= PP_FORMAT_420;
	} else if (vi.IsYUY2()) {
		Flags |= PP_FORMAT_422;
		SWSTo422P = GetSwsContext(
			vi.width, vi.height, PIX_FMT_YUYV422, GetAssumedColorSpace(vi.width, vi.height), AVCOL_RANGE_UNSPECIFIED,
			vi.width, vi.height, PIX_FMT_YUV422P, GetAssumedColorSpace(vi.width, vi.height), AVCOL_RANGE_UNSPECIFIED,
			Flags | SWS_BICUBIC);
		SWSFrom422P = GetSwsContext(
			vi.width, vi.height, PIX_FMT_YUV422P, GetAssumedColorSpace(vi.width, vi.height), AVCOL_RANGE_UNSPECIFIED,
			vi.width, vi.height, PIX_FMT_YUYV422, GetAssumedColorSpace(vi.width, vi.height), AVCOL_RANGE_UNSPECIFIED,
			Flags | SWS_BICUBIC);
		avpicture_alloc(&InputPicture, PIX_FMT_YUV422P, vi.width, vi.height);
		avpicture_alloc(&OutputPicture, PIX_FMT_YUV422P, vi.width, vi.height);
	} else {
		Env->ThrowError("FFPP: Only YV12 and YUY2 video supported");
	}

	/* Flags as passed to pp_get_context will potentially no longer be the same int value,
	 * but it will still have the correct binary representation (which is the important part). */
	PPContext = pp_get_context(vi.width, vi.height, (int)Flags);
	if (!PPContext)
		Env->ThrowError("FFPP: Failed to create context");
}

FFPP::~FFPP() {
	if (PPMode)
		pp_free_mode(PPMode);
	if (PPContext)
		pp_free_context(PPContext);
	if (SWSTo422P)
		sws_freeContext(SWSTo422P);
	if (SWSFrom422P)
		sws_freeContext(SWSFrom422P);
	avpicture_free(&InputPicture);
	avpicture_free(&OutputPicture);
}

PVideoFrame FFPP::GetFrame(int n, IScriptEnvironment* Env) {
	PVideoFrame Src = child->GetFrame(n, Env);
	PVideoFrame Dst = Env->NewVideoFrame(vi);

	if (vi.IsYV12()) {
		const uint8_t *SrcData[3] = {(uint8_t *)Src->GetReadPtr(PLANAR_Y), (uint8_t *)Src->GetReadPtr(PLANAR_U), (uint8_t *)Src->GetReadPtr(PLANAR_V)};
		int SrcStride[3] = {Src->GetPitch(PLANAR_Y), Src->GetPitch(PLANAR_U), Src->GetPitch(PLANAR_V)};
		uint8_t *DstData[3] = {Dst->GetWritePtr(PLANAR_Y), Dst->GetWritePtr(PLANAR_U), Dst->GetWritePtr(PLANAR_V)};
		int DstStride[3] = {Dst->GetPitch(PLANAR_Y), Dst->GetPitch(PLANAR_U), Dst->GetPitch(PLANAR_V)};

		pp_postprocess(SrcData, SrcStride, DstData, DstStride, vi.width, vi.height, NULL, 0, PPMode, PPContext, 0);
	} else if (vi.IsYUY2()) {
		const uint8_t *SrcData[1] = {(uint8_t *)Src->GetReadPtr()};
		int SrcStride[1] = {Src->GetPitch()};
		sws_scale(SWSTo422P, SrcData, SrcStride, 0, vi.height, InputPicture.data, InputPicture.linesize);
		
		pp_postprocess(const_cast<const uint8_t **>(InputPicture.data), InputPicture.linesize, OutputPicture.data, OutputPicture.linesize, vi.width, vi.height, NULL, 0, PPMode, PPContext, 0);

		uint8_t *DstData[1] = {Dst->GetWritePtr()};
		int DstStride[1] = {Dst->GetPitch()};
		sws_scale(SWSFrom422P, OutputPicture.data, OutputPicture.linesize, 0, vi.height, DstData, DstStride);
	}

	return Dst;
}

#endif // FFMS_USE_POSTPROC
