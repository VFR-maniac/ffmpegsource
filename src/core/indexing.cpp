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

#include "indexing.h"

#include "codectype.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>


extern "C" {
#include <libavutil/avutil.h>

#if LIBAVUTIL_VERSION_INT > AV_VERSION_INT(50, 40, 1)
#include <libavutil/sha.h>
#else
extern const int av_sha_size;
struct AVSHA;
int av_sha_init(struct AVSHA* context, int bits);
void av_sha_update(struct AVSHA* context, const uint8_t* data, unsigned int len);
void av_sha_final(struct AVSHA* context, uint8_t *digest);
#endif

#include <zlib.h>
}

#undef max

#define INDEXID 0x53920873
#ifdef __MINGW64__
	#define ARCH 1
#elif defined(__MINGW32__)
	#define ARCH 2
#elif defined(_WIN64)
	#define ARCH 6
#elif defined(_WIN32)
	#define ARCH 3
#elif defined(__i386__) //*nix 32bit
	#define ARCH 4
#else //*nix 64bit
	#define ARCH 5
#endif

extern bool HasHaaliMPEG;
extern bool HasHaaliOGG;

#ifndef FFMS_USE_POSTPROC
unsigned postproc_version() { return 0; } // ugly workaround to avoid lots of ifdeffing
#endif // FFMS_USE_POSTPROC

struct IndexHeader {
	uint32_t Id;
	uint32_t Version;
	uint32_t Arch;
	uint32_t Tracks;
	uint32_t Decoder;
	uint32_t LAVUVersion;
	uint32_t LAVFVersion;
	uint32_t LAVCVersion;
	uint32_t LSWSVersion;
	uint32_t LPPVersion;
	int64_t FileSize;
	uint8_t FileSignature[20];
};

struct TrackHeader {
	uint32_t TT;
	uint32_t Frames;
	int64_t Num;
	int64_t Den;
	uint32_t UseDTS;
	uint32_t HasTS;
};


SharedVideoContext::SharedVideoContext(bool FreeCodecContext) {
	CodecContext = NULL;
	Parser = NULL;
	BitStreamFilter = NULL;
	this->FreeCodecContext = FreeCodecContext;
	TCC = NULL;
}

SharedVideoContext::~SharedVideoContext() {
	if (CodecContext) {
		avcodec_close(CodecContext);
		if (FreeCodecContext)
			av_freep(&CodecContext);
	}
	av_parser_close(Parser);
	if (BitStreamFilter)
		av_bitstream_filter_close(BitStreamFilter);
	delete TCC;
}

SharedAudioContext::SharedAudioContext(bool FreeCodecContext) {
	W64Writer = NULL;
	CodecContext = NULL;
	CurrentSample = 0;
	TCC = NULL;
	this->FreeCodecContext = FreeCodecContext;
}

SharedAudioContext::~SharedAudioContext() {
	delete W64Writer;
	if (CodecContext) {
		avcodec_close(CodecContext);
		if (FreeCodecContext)
			av_freep(&CodecContext);
	}
	delete TCC;
}

TFrameInfo::TFrameInfo(int64_t PTS, int64_t SampleStart, unsigned int SampleCount, int RepeatPict, bool KeyFrame, int64_t FilePos, unsigned int FrameSize, int FrameType)
: SampleStart(SampleStart)
, SampleCount(SampleCount)
, FilePos(FilePos)
, FrameSize(FrameSize)
, OriginalPos(0)
, FrameType(FrameType)
{
	this->PTS = PTS;
	this->RepeatPict = RepeatPict;
	this->KeyFrame = KeyFrame;
}

TFrameInfo TFrameInfo::VideoFrameInfo(int64_t PTS, int RepeatPict, bool KeyFrame, int FrameType, int64_t FilePos, unsigned int FrameSize) {
	return TFrameInfo(PTS, 0, 0, RepeatPict, KeyFrame, FilePos, FrameSize, FrameType);
}

TFrameInfo TFrameInfo::AudioFrameInfo(int64_t PTS, int64_t SampleStart, int64_t SampleCount, bool KeyFrame, int64_t FilePos, unsigned int FrameSize) {
	return TFrameInfo(PTS, SampleStart, static_cast<unsigned int>(SampleCount), 0, KeyFrame, FilePos, FrameSize, 0);
}

void FFMS_Track::WriteTimecodes(const char *TimecodeFile) {
	ffms_fstream Timecodes(TimecodeFile, std::ios::out | std::ios::trunc);

	if (!Timecodes.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + TimecodeFile + "' for writing");

	Timecodes << "# timecode format v2\n";

	for (iterator Cur = begin(); Cur != end(); ++Cur)
		Timecodes << std::fixed << ((Cur->PTS * TB.Num) / (double)TB.Den) << "\n";
}

int FFMS_Track::FrameFromPTS(int64_t PTS) {
	for (int i = 0; i < static_cast<int>(size()); i++)
		if (at(i).PTS == PTS)
			return i;
	return -1;
}

int FFMS_Track::FrameFromPos(int64_t Pos) {
	for (int i = 0; i < static_cast<int>(size()); i++)
		if (at(i).FilePos == Pos)
			return i;
	return -1;
}

static bool PTSComparison(TFrameInfo FI1, TFrameInfo FI2) {
	return FI1.PTS < FI2.PTS;
}

int FFMS_Track::ClosestFrameFromPTS(int64_t PTS) {
	TFrameInfo F;
	F.PTS = PTS;

	iterator Pos = std::lower_bound(begin(), end(), F, PTSComparison);
	if (Pos == end())
		return size() - 1;
	int Frame = std::distance(begin(), Pos);
	if (Pos == begin() || FFABS(Pos->PTS - PTS) <= FFABS((Pos - 1)->PTS - PTS))
		return Frame;
	return Frame - 1;
}

int FFMS_Track::FindClosestVideoKeyFrame(int Frame) {
	Frame = FFMIN(FFMAX(Frame, 0), static_cast<int>(size()) - 1);
	for (; Frame > 0 && !at(Frame).KeyFrame; Frame--) ;
	for (; Frame > 0 && !at(at(Frame).OriginalPos).KeyFrame; Frame--) ;
	return Frame;
}

void FFMS_Track::MaybeReorderFrames() {
	// First check if we need to do anything
	bool has_b_frames = false;
	for (size_t i = 1; i < size(); ++i) {
		// If the timestamps are already out of order, then they actually are
		// presentation timestamps and we don't need to do anything
		if (at(i).PTS < at(i - 1).PTS)
			return;

		if (at(i).FrameType == AV_PICTURE_TYPE_B) {
			has_b_frames = true;

			// Reordering files with multiple b-frames is currently not
			// supported
			if (at(i - 1).FrameType == AV_PICTURE_TYPE_B)
				return;
		}
	}

	// Don't need to do anything if there are no b-frames as presentation order
	// equals decoding order
	if (!has_b_frames)
		return;

	// Swap the presentation time stamps of each b-frame with that of the frame
	// before it
	for (size_t i = 1; i < size(); ++i) {
		if (at(i).FrameType == AV_PICTURE_TYPE_B)
			std::swap(at(i).PTS, at(i - 1).PTS);
	}
}

FFMS_Track::FFMS_Track() {
	this->TT = FFMS_TYPE_UNKNOWN;
	this->TB.Num = 0;
	this->TB.Den = 0;
	this->UseDTS = false;
	this->HasTS = true;
}

FFMS_Track::FFMS_Track(int64_t Num, int64_t Den, FFMS_TrackType TT, bool UseDTS, bool HasTS) {
	this->TT = TT;
	this->TB.Num = Num;
	this->TB.Den = Den;
	this->UseDTS = UseDTS;
	this->HasTS = HasTS;
}

void FFMS_Index::CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]) {
	FILE *SFile = ffms_fopen(Filename,"rb");
	if (!SFile)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + Filename + "' for hashing");

	std::vector<uint8_t> FileBuffer(1024*1024);
	std::vector<uint8_t> ctxmem(av_sha_size);
	AVSHA *ctx = (AVSHA*)(&ctxmem[0]);
	av_sha_init(ctx, 160);

	try {
		size_t BytesRead = fread(&FileBuffer[0], 1, FileBuffer.size(), SFile);
		if (ferror(SFile) && !feof(SFile))
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Failed to read '") + Filename + "' for hashing");

		av_sha_update(ctx, &FileBuffer[0], BytesRead);

		fseeko(SFile, -(int)FileBuffer.size(), SEEK_END);
		BytesRead = fread(&FileBuffer[0], 1, FileBuffer.size(), SFile);
		if (ferror(SFile) && !feof(SFile)) {
			std::ostringstream buf;
			buf << "Failed to seek with offset " << FileBuffer.size() << " from file end in '" << Filename << "' for hashing";
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, buf.str());
		}

		av_sha_update(ctx, &FileBuffer[0], BytesRead);

		fseeko(SFile, 0, SEEK_END);
		if (ferror(SFile))
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Failed to seek to end of '") + Filename + "' for hashing");

		*Filesize = ftello(SFile);
	}
	catch (...) {
		fclose(SFile);
		av_sha_final(ctx, Digest);
		throw;
	}
	fclose(SFile);
	av_sha_final(ctx, Digest);
}


int FFMS_Index::AddRef() {
	return ++RefCount;
}

int FFMS_Index::Release() {
	int Temp = --RefCount;
	if (!RefCount)
		delete this;
	return Temp;
}

void FFMS_Index::Sort() {
	for (FFMS_Index::iterator Cur = begin(); Cur != end(); ++Cur) {
		// With some formats (such as Vorbis) a bad final packet results in a
		// frame with PTS 0, which we don't want to sort to the beginning
		if (Cur->size() > 2 && Cur->front().PTS >= Cur->back().PTS) Cur->pop_back();

		for (size_t i = 0; i < Cur->size(); i++)
			Cur->at(i).OriginalPos = i;

		if (Cur->TT != FFMS_TYPE_VIDEO)
			continue;

		Cur->MaybeReorderFrames();

		std::sort(Cur->begin(), Cur->end(), PTSComparison);

		std::vector<size_t> ReorderTemp;
		ReorderTemp.resize(Cur->size());

		for (size_t i = 0; i < Cur->size(); i++)
			ReorderTemp[i] = Cur->at(i).OriginalPos;

		for (size_t i = 0; i < Cur->size(); i++)
			Cur->at(ReorderTemp[i]).OriginalPos = i;
	}
}

bool FFMS_Index::CompareFileSignature(const char *Filename) {
	int64_t CFilesize;
	uint8_t CDigest[20];
	CalculateFileSignature(Filename, &CFilesize, CDigest);
	return (CFilesize == Filesize && !memcmp(CDigest, Digest, sizeof(Digest)));
}

#define CHUNK 65536

static unsigned int z_def(ffms_fstream *IndexStream, z_stream *stream, void *in, size_t in_sz, int finish) {
	unsigned int total = 0, have;
	int ret;
	char out[CHUNK];

	if (!finish && (in_sz == 0 || in == NULL)) return 0;

	stream->next_in = (Bytef*) in;
	stream->avail_in = in_sz;
	do {
		do {
			stream->avail_out = CHUNK;
			stream->next_out = (Bytef*) out;
			ret = deflate(stream, finish ? Z_FINISH : Z_NO_FLUSH);
			have = CHUNK - stream->avail_out;
			if (have) IndexStream->write(out, have);
			total += have;
		} while (stream->avail_out == 0);
	} while (finish && ret != Z_STREAM_END);
	if (finish) deflateEnd(stream);
	return total;
}

void FFMS_Index::WriteIndex(const char *IndexFile) {
	ffms_fstream IndexStream(IndexFile, std::ios::out | std::ios::binary | std::ios::trunc);

	if (!IndexStream.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + IndexFile + "' for writing");

	z_stream stream;
	memset(&stream, 0, sizeof(z_stream));
	if (deflateInit(&stream, 9) != Z_OK) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to initialize zlib");
	}

	// Write the index file header
	IndexHeader IH;
	IH.Id = INDEXID;
	IH.Version = FFMS_VERSION;
	IH.Arch = ARCH;
	IH.Tracks = size();
	IH.Decoder = Decoder;
	IH.LAVUVersion = avutil_version();
	IH.LAVFVersion = avformat_version();
	IH.LAVCVersion = avcodec_version();
	IH.LSWSVersion = swscale_version();
	IH.LPPVersion = postproc_version();
	IH.FileSize = Filesize;
	memcpy(IH.FileSignature, Digest, sizeof(Digest));

	z_def(&IndexStream, &stream, &IH, sizeof(IndexHeader), 0);

	for (unsigned int i = 0; i < IH.Tracks; i++) {
		FFMS_Track &ctrack = at(i);
		TrackHeader TH;
		TH.TT = ctrack.TT;
		TH.Frames = ctrack.size();
		TH.Num = ctrack.TB.Num;;
		TH.Den = ctrack.TB.Den;
		TH.UseDTS = ctrack.UseDTS;
		TH.HasTS = ctrack.HasTS;

		FFMS_Track temptrack;
		temptrack.resize(TH.Frames);

		if (TH.Frames)
			temptrack[0] = ctrack[0];

		for (size_t j = 1; j < ctrack.size(); j++) {
			temptrack[j] = ctrack[j];
			temptrack[j].FilePos = ctrack[j].FilePos - ctrack[j - 1].FilePos;
			temptrack[j].OriginalPos = ctrack[j].OriginalPos - ctrack[j - 1].OriginalPos;
			temptrack[j].PTS = ctrack[j].PTS - ctrack[j - 1].PTS;
			temptrack[j].SampleStart = ctrack[j].SampleStart - ctrack[j - 1].SampleStart;
		}

		z_def(&IndexStream, &stream, &TH, sizeof(TrackHeader), 0);
		if (TH.Frames)
			z_def(&IndexStream, &stream, FFMS_GET_VECTOR_PTR(temptrack), TH.Frames * sizeof(TFrameInfo), 0);
	}
	z_def(&IndexStream, &stream, NULL, 0, 1);
}

static unsigned int z_inf(ffms_fstream *Index, z_stream *stream, void *in, size_t in_sz, void *out, size_t out_sz) {
	if (out_sz == 0 || out == 0) return 0;
	stream->next_out = (Bytef*) out;
	stream->avail_out = out_sz;

	do {
		if (stream->avail_in) memmove(in, stream->next_in, stream->avail_in);
		Index->read(((char*)in) + stream->avail_in, in_sz - stream->avail_in);
		stream->next_in = (Bytef*) in;
		stream->avail_in += Index->gcount();

		switch (inflate(stream, Z_SYNC_FLUSH)) {
		case Z_NEED_DICT:
			inflateEnd(stream);
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Dictionary error.");
		case Z_DATA_ERROR:
			inflateEnd(stream);
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Data error.");
		case Z_MEM_ERROR:
			inflateEnd(stream);
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Memory error.");
		case Z_STREAM_END:
			inflateEnd(stream);
			return out_sz - stream->avail_out;
		}

	} while (stream->avail_out);
	return out_sz;
}

void FFMS_Index::ReadIndex(const char *IndexFile) {
	ffms_fstream Index(IndexFile, std::ios::in | std::ios::binary);

	if (!Index.is_open())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Failed to open '") + IndexFile + "' for reading");

	z_stream stream;
	memset(&stream, 0, sizeof(z_stream));
	unsigned char in[CHUNK];
	if (inflateInit(&stream) != Z_OK)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			"Failed to initialize zlib");

	// Read the index file header
	IndexHeader IH;
	z_inf(&Index, &stream,  &in, CHUNK, &IH, sizeof(IndexHeader));

	if (IH.Id != INDEXID)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not a valid index file");

	if (IH.Version != FFMS_VERSION)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' is not the expected index version");

	if (IH.Arch != ARCH)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("'") + IndexFile + "' was not made with this FFMS2 binary");

	if (IH.LAVUVersion != avutil_version() || IH.LAVFVersion != avformat_version() ||
		IH.LAVCVersion != avcodec_version() || IH.LSWSVersion != swscale_version() ||
		IH.LPPVersion != postproc_version())
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("A different FFmpeg build was used to create '") + IndexFile + "'");

	if (!(IH.Decoder & FFMS_GetEnabledSources()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_NOT_AVAILABLE,
			"The source which this index was created with is not available");

	Decoder = IH.Decoder;
	Filesize = IH.FileSize;
	memcpy(Digest, IH.FileSignature, sizeof(Digest));

	try {
		for (unsigned int i = 0; i < IH.Tracks; i++) {
			TrackHeader TH;
			z_inf(&Index, &stream, &in, CHUNK, &TH, sizeof(TrackHeader));
			push_back(FFMS_Track(TH.Num, TH.Den, static_cast<FFMS_TrackType>(TH.TT), TH.UseDTS != 0, TH.HasTS != 0));
			FFMS_Track &ctrack = at(i);

			if (TH.Frames) {
				ctrack.resize(TH.Frames);
				z_inf(&Index, &stream, &in, CHUNK, FFMS_GET_VECTOR_PTR(ctrack), TH.Frames * sizeof(TFrameInfo));
			}

			for (size_t j = 1; j < ctrack.size(); j++) {
				ctrack[j].FilePos = ctrack[j].FilePos + ctrack[j - 1].FilePos;
				ctrack[j].OriginalPos = ctrack[j].OriginalPos + ctrack[j - 1].OriginalPos;
				ctrack[j].PTS = ctrack[j].PTS + ctrack[j - 1].PTS;
				ctrack[j].SampleStart = ctrack[j].SampleStart + ctrack[j - 1].SampleStart;
			}
		}
	}
	catch (FFMS_Exception const&) {
		throw;
	}
	catch (...) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Unknown error while reading index information in '") + IndexFile + "'");
	}
}

FFMS_Index::FFMS_Index() : RefCount(1) {
}

FFMS_Index::FFMS_Index(int64_t Filesize, uint8_t Digest[20]) : RefCount(1), Filesize(Filesize) {
	memcpy(this->Digest, Digest, sizeof(this->Digest));
}

void FFMS_Indexer::SetIndexMask(int IndexMask) {
	this->IndexMask = IndexMask;
}

void FFMS_Indexer::SetDumpMask(int DumpMask) {
	this->DumpMask = DumpMask;
}

void FFMS_Indexer::SetErrorHandling(int ErrorHandling) {
	if (ErrorHandling != FFMS_IEH_ABORT && ErrorHandling != FFMS_IEH_CLEAR_TRACK &&
		ErrorHandling != FFMS_IEH_STOP_TRACK && ErrorHandling != FFMS_IEH_IGNORE)
		throw FFMS_Exception(FFMS_ERROR_INDEXING, FFMS_ERROR_INVALID_ARGUMENT,
			"Invalid error handling mode specified");
	this->ErrorHandling = ErrorHandling;
}

void FFMS_Indexer::SetProgressCallback(TIndexCallback IC, void *ICPrivate) {
	this->IC = IC;
	this->ICPrivate = ICPrivate;
}

void FFMS_Indexer::SetAudioNameCallback(TAudioNameCallback ANC, void *ANCPrivate) {
	this->ANC = ANC;
	this->ANCPrivate = ANCPrivate;
}

FFMS_Indexer *FFMS_Indexer::CreateIndexer(const char *Filename, FFMS_Sources Demuxer) {
	AVFormatContext *FormatContext = NULL;

	if (avformat_open_input(&FormatContext, Filename, NULL, NULL) != 0)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Can't open '") + Filename + "'");

	// Demuxer was not forced, probe for the best one to use
	if (Demuxer == FFMS_SOURCE_DEFAULT) {
		// Do matroska indexing instead?
		if (!strncmp(FormatContext->iformat->name, "matroska", 8)) {
			avformat_close_input(&FormatContext);
			return new FFMatroskaIndexer(Filename);
		}

#ifdef HAALISOURCE
		// Do haali ts indexing instead?
		if (HasHaaliMPEG && (!strcmp(FormatContext->iformat->name, "mpeg") || !strcmp(FormatContext->iformat->name, "mpegts"))) {
			avformat_close_input(&FormatContext);
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIMPEG);
		}

		if (HasHaaliOGG && !strcmp(FormatContext->iformat->name, "ogg")) {
			avformat_close_input(&FormatContext);
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIOGG);
		}
#endif

		return new FFLAVFIndexer(Filename, FormatContext);
	}

	// someone forced a demuxer, use it
	if (Demuxer != FFMS_SOURCE_LAVF)
		avformat_close_input(&FormatContext);
#if !defined(HAALISOURCE)
	if (Demuxer == FFMS_SOURCE_HAALIOGG || Demuxer == FFMS_SOURCE_HAALIMPEG) {
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Your binary was not compiled with support for Haali's DirectShow parsers");
	}
#endif // !defined(HAALISOURCE)

	switch (Demuxer) {
		case FFMS_SOURCE_LAVF:
			return new FFLAVFIndexer(Filename, FormatContext);
#ifdef HAALISOURCE
		case FFMS_SOURCE_HAALIOGG:
			if (!HasHaaliOGG)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Haali's Ogg parser is not available");
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIOGG);
		case FFMS_SOURCE_HAALIMPEG:
			if (!HasHaaliMPEG)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_NOT_AVAILABLE, "Haali's MPEG PS/TS parser is not available");
			return new FFHaaliIndexer(Filename, FFMS_SOURCE_HAALIMPEG);
#endif
		case FFMS_SOURCE_MATROSKA:
			return new FFMatroskaIndexer(Filename);
		default:
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_INVALID_ARGUMENT, "Invalid demuxer requested");
	}
}

FFMS_Indexer::FFMS_Indexer(const char *Filename)
: IndexMask(0)
, DumpMask(0)
, ErrorHandling(FFMS_IEH_CLEAR_TRACK)
, IC(0)
, ICPrivate(0)
, ANC(0)
, ANCPrivate(0)
, SourceFile(Filename)
, DecodingBuffer(AVCODEC_MAX_AUDIO_FRAME_SIZE * 10)
{
	FFMS_Index::CalculateFileSignature(Filename, &Filesize, Digest);
}

FFMS_Indexer::~FFMS_Indexer() {

}

void FFMS_Indexer::WriteAudio(SharedAudioContext &AudioContext, FFMS_Index *Index, int Track, int DBSize) {
	// Delay writer creation until after an audio frame has been decoded. This ensures that all parameters are known when writing the headers.
	if (DBSize <= 0) return;

	if (!AudioContext.W64Writer) {
		FFMS_AudioProperties AP;
		FillAP(AP, AudioContext.CodecContext, (*Index)[Track]);
		int FNSize = (*ANC)(SourceFile.c_str(), Track, &AP, NULL, 0, ANCPrivate);
		if (FNSize <= 0) {
			DumpMask = DumpMask & ~(1 << Track);
			return;
		}

		std::vector<char> WName(FNSize);
		(*ANC)(SourceFile.c_str(), Track, &AP, &WName[0], FNSize, ANCPrivate);
		std::string WN(&WName[0]);
		try {
			AudioContext.W64Writer =
				new Wave64Writer(WN.c_str(),
					av_get_bytes_per_sample(AudioContext.CodecContext->sample_fmt),
					AudioContext.CodecContext->channels,
					AudioContext.CodecContext->sample_rate,
					(AudioContext.CodecContext->sample_fmt == AV_SAMPLE_FMT_FLT) || (AudioContext.CodecContext->sample_fmt == AV_SAMPLE_FMT_DBL));
		} catch (...) {
			throw FFMS_Exception(FFMS_ERROR_WAVE_WRITER, FFMS_ERROR_FILE_WRITE,
				"Failed to write wave data");
		}
	}

	AudioContext.W64Writer->WriteData(&DecodingBuffer[0], DBSize);
}

int64_t FFMS_Indexer::IndexAudioPacket(int Track, AVPacket *Packet, SharedAudioContext &Context, FFMS_Index &TrackIndices) {
	AVCodecContext *CodecContext = Context.CodecContext;
	int64_t StartSample = Context.CurrentSample;
	int Read = 0;
	while (Packet->size > 0) {
		int dbsize = AVCODEC_MAX_AUDIO_FRAME_SIZE*10;
		int Ret = avcodec_decode_audio3(CodecContext, (int16_t *)&DecodingBuffer[0], &dbsize, Packet);
		if (Ret < 0) {
			if (ErrorHandling == FFMS_IEH_ABORT) {
				throw FFMS_Exception(FFMS_ERROR_CODEC, FFMS_ERROR_DECODING, "Audio decoding error");
			} else if (ErrorHandling == FFMS_IEH_CLEAR_TRACK) {
				TrackIndices[Track].clear();
				IndexMask &= ~(1 << Track);
			} else if (ErrorHandling == FFMS_IEH_STOP_TRACK) {
				IndexMask &= ~(1 << Track);
			}
			break;
		}
		Packet->size -= Ret;
		Packet->data += Ret;
		Read += Ret;

		CheckAudioProperties(Track, CodecContext);

		if (dbsize > 0)
			Context.CurrentSample += dbsize / (av_get_bytes_per_sample(CodecContext->sample_fmt) * CodecContext->channels);

		if (DumpMask & (1 << Track))
			WriteAudio(Context, &TrackIndices, Track, dbsize);
	}
	Packet->size += Read;
	Packet->data -= Read;
	return Context.CurrentSample - StartSample;
}

void FFMS_Indexer::CheckAudioProperties(int Track, AVCodecContext *Context) {
	std::map<int, FFMS_AudioProperties>::iterator it = LastAudioProperties.find(Track);
	if (it == LastAudioProperties.end()) {
		FFMS_AudioProperties &AP = LastAudioProperties[Track];
		AP.SampleRate = Context->sample_rate;
		AP.SampleFormat = Context->sample_fmt;
		AP.Channels = Context->channels;
	}
	else if (it->second.SampleRate   != Context->sample_rate ||
			 it->second.SampleFormat != Context->sample_fmt ||
			 it->second.Channels     != Context->channels) {
		std::ostringstream buf;
		buf <<
			"Audio format change detected. This is currently unsupported."
			<< " Channels: " << it->second.Channels << " -> " << Context->channels << ";"
			<< " Sample rate: " << it->second.SampleRate << " -> " << Context->sample_rate << ";"
			<< " Sample format: " << GetLAVCSampleFormatName((AVSampleFormat)it->second.SampleFormat) << " -> "
			<< GetLAVCSampleFormatName(Context->sample_fmt);
		throw FFMS_Exception(FFMS_ERROR_UNSUPPORTED, FFMS_ERROR_DECODING, buf.str());
	}
}

void FFMS_Indexer::ParseVideoPacket(SharedVideoContext &VideoContext, AVPacket &pkt, int *RepeatPict, int *FrameType) {
	if (VideoContext.Parser) {
		uint8_t *OB;
		int OBSize;
		av_parser_parse2(VideoContext.Parser,
			VideoContext.CodecContext,
			&OB, &OBSize,
			pkt.data, pkt.size,
			pkt.pts, pkt.dts, pkt.pos);

		*RepeatPict = VideoContext.Parser->repeat_pict;
		*FrameType = VideoContext.Parser->pict_type;
	}
}
