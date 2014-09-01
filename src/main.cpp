#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include "include/vpu.h"
#include "include/vpu_type.h"
#include "include/vpu_api.h"

#undef LOG_TAG
#define LOG_TAG "vpu_api_demo"

static RK_U32 VPU_API_DEMO_DEBUG_DISABLE = 0;

#define DEMO_ERR_RET(err) do { ret = err; goto DEMO_OUT; } while (0)

#define VPU_DEMO_LOG(fmt, args...) printf(fmt, ## args)


typedef enum VPU_API_DEMO_RET {
    VPU_DEMO_OK = 0,
    VPU_DEMO_PARSE_HELP_OK  = 1,

    VPU_DEMO_ERROR_BASE     = -100,
    ERROR_INVALID_PARAM     = VPU_DEMO_ERROR_BASE - 1,
    ERROR_INVALID_STREAM    = VPU_DEMO_ERROR_BASE - 2,
    ERROR_IO                = VPU_DEMO_ERROR_BASE - 3,
    ERROR_MEMORY            = VPU_DEMO_ERROR_BASE - 4,
    ERROR_INIT_VPU          = VPU_DEMO_ERROR_BASE - 5,
    ERROR_VPU_DECODE        = VPU_DEMO_ERROR_BASE - 90,
} VPU_API_DEMO_RET;

typedef struct VpuApiCmd {
    RK_U8* name;
    RK_U8* argname;
    RK_U8* help;
}VpuApiCmd_t;

typedef struct VpuApiDemoCmdContext {
    RK_U32  width;
    RK_U32  height;
    CODEC_TYPE  codec_type;
    OMX_ON2_VIDEO_CODINGTYPE coding;
    RK_U8   input_file[200];
    RK_U8   output_file[200];
    RK_U8   have_input;
    RK_U8   have_output;
    RK_U8   disable_debug;
    RK_U32  record_frames;
    RK_S64  record_start_ms;
}VpuApiDemoCmdContext_t;

static VpuApiCmd_t vpuApiCmd[] = {
    {"i",               "input_file",           "input bitstream file"},
    {"o",               "output_file",          "output bitstream file, "},
    {"w",               "width",                "the width of input bitstream"},
    {"h",               "height",               "the height of input bitstream"},
    {"t",               "codec_type",           "the codec type, dec: deoder, enc: encoder, default[dec]"},
    {"coding",          "coding_type",          "encoding type of the bitstream"},
    {"vframes",         "number",               "set the number of video frames to record"},
    {"ss",              "time_off",             "set the start time offset, use Ms as the unit."},
    {"d",               "disable",              "disable the debug output info."},
};

static void show_usage()
{
    VPU_DEMO_LOG("usage: vpu_apiDemo [options] input_file, \n\n");

    VPU_DEMO_LOG("Getting help:\n");
    VPU_DEMO_LOG("-help  --print options of vpu api demo\n");
}

static RK_S32 show_help()
{
    VPU_DEMO_LOG("usage: vpu_apiDemo [options] input_file, \n\n");

    RK_S32 i =0;
    RK_U32 n = sizeof(vpuApiCmd)/sizeof(VpuApiCmd_t);
    for (i =0; i <n; i++) {
        VPU_DEMO_LOG("-%s  %s\t\t%s\n",
            vpuApiCmd[i].name, vpuApiCmd[i].argname, vpuApiCmd[i].help);
    }

    return 0;
}

static RK_S32 parse_options(int argc, char **argv, VpuApiDemoCmdContext_t* cmdCxt)
{
    RK_S8 *opt;
    RK_U32 optindex, handleoptions = 1, ret =0;

    if ((argc <2) || (cmdCxt == NULL)) {
        VPU_DEMO_LOG("vpu api demo, input parameter invalid\n");
        show_usage();
        return ERROR_INVALID_PARAM;
    }

    /* parse options */
    optindex = 1;
    while (optindex < argc) {
        opt = argv[optindex++];

        if (handleoptions && opt[0] == '-' && opt[1] != '\0') {
            if (opt[1] == '-') {
                if (opt[2] != '\0') {
                    opt++;
                } else {
                     handleoptions = 0;
                    continue;
                }
            }

            opt++;

            switch (*opt) {
                case 'i':
                    if (argv[optindex]) {
                        memcpy(cmdCxt->input_file, argv[optindex], strlen(argv[optindex]));
                        cmdCxt->input_file[strlen(argv[optindex])] = '\0';
                        cmdCxt->have_input = 1;
                    } else {
                        VPU_DEMO_LOG("input file is invalid\n");
                        ret = -1;
                        goto PARSE_OPINIONS_OUT;
                    }
                    break;
                case 'o':
                    if (argv[optindex]) {
                        memcpy(cmdCxt->output_file, argv[optindex], strlen(argv[optindex]));
                        cmdCxt->output_file[strlen(argv[optindex])] = '\0';
                        cmdCxt->have_output = 1;
                        break;
                    } else {
                        VPU_DEMO_LOG("out file is invalid\n");
                        ret = -1;
                        goto PARSE_OPINIONS_OUT;
                    }
                case 'd':
                    cmdCxt->disable_debug = 1;
                    break;
                case 'w':
                    if (argv[optindex]) {
                        cmdCxt->width = atoi(argv[optindex]);
                        break;
                    } else {
                        VPU_DEMO_LOG("input width is invalid\n");
                        ret = -1;
                        goto PARSE_OPINIONS_OUT;
                    }
                case 'h':
                    if ((*(opt+1) != '\0') && !strncmp(opt, "help", 4)) {
                        show_help();
                        ret = VPU_DEMO_PARSE_HELP_OK;
                        goto PARSE_OPINIONS_OUT;
                    } else if (argv[optindex]) {
                        cmdCxt->height = atoi(argv[optindex]);
                    } else {
                        VPU_DEMO_LOG("input height is invalid\n");
                        ret = -1;
                        goto PARSE_OPINIONS_OUT;
                    }
                    break;
                case 't':
                    if (argv[optindex]) {
                        cmdCxt->codec_type = atoi(argv[optindex]);
                        break;
                    } else {
                        VPU_DEMO_LOG("input codec_type is invalid\n");
                        ret = -1;
                        goto PARSE_OPINIONS_OUT;
                    }

                default:
                    if ((*(opt+1) != '\0') && argv[optindex]) {
                        if (!strncmp(opt, "coding", 6)) {

                            VPU_DEMO_LOG("coding, argv[optindex]: %s",
                                argv[optindex]);

                            cmdCxt->coding = atoi(argv[optindex]);
                        } else if (!strncmp(opt, "vframes", 7)) {
                            cmdCxt->record_frames = atoi(argv[optindex]);
                        } else if (!strncmp(opt, "ss", 2)) {
                            cmdCxt->record_start_ms = atoi(argv[optindex]);
                        } else {
                            ret = -1;
                            goto PARSE_OPINIONS_OUT;
                        }
                    } else {
                        ret = -1;
                        goto PARSE_OPINIONS_OUT;
                    }
                    break;
            }

            optindex += ret;
        }
    }

PARSE_OPINIONS_OUT:
    if (ret <0) {
        VPU_DEMO_LOG("vpu api demo, input parameter invalid\n");
        show_usage();
        return ERROR_INVALID_PARAM;
    }
    return ret;
}

static RK_S32 readBytesFromFile(RK_U8* buf, RK_S32 aBytes, FILE* file)
{
    if ((NULL ==buf) || (NULL ==file) || (0 ==aBytes)) {
        return -1;
    }

    RK_S32 ret = fread(buf, 1, aBytes, file);
	if(ret != aBytes)
	{
		VPU_DEMO_LOG("read %d bytes from file fail", aBytes);
        return -1;
	}

    return 0;
}

int main(int argc, char **argv)
{
    VPU_DEMO_LOG("/*******  vpu api demo in *******/");

    FILE* pInFile = NULL;
    FILE* pOutFile = NULL;
    struct VpuCodecContext* ctx = NULL;
    RK_S32 fileSize =0, pkt_size =0;
    RK_S32 ret = 0, frame_count = 0;

    DecoderOut_t    decOut;
    VideoPacket_t demoPkt;
    VideoPacket_t* pkt =NULL;
    DecoderOut_t *pOut = NULL;

    VPU_FRAME *frame = NULL;
    RK_S64 fakeTimeUs =0;
    RK_U8* pExtra = NULL;
    RK_U32 extraSize = 0;
    VpuApiDemoCmdContext_t demoCmdCtx;

    VPU_API_DEMO_DEBUG_DISABLE = 0;

    if (argc == 1) {
        show_usage();
        VPU_DEMO_LOG("vpu api demo complete directly\n");
        return 0;
    }

    VpuApiDemoCmdContext_t* cmd = &demoCmdCtx;
    memset (cmd, 0, sizeof(VpuApiDemoCmdContext_t));
    cmd->codec_type = CODEC_DECODER;
    if ((ret = parse_options(argc, argv, cmd)) !=0) {
        if (ret == VPU_DEMO_PARSE_HELP_OK) {
            return 0;
        }

        VPU_DEMO_LOG("parse_options fail\n\n");
        show_usage();
        DEMO_ERR_RET(ERROR_INVALID_PARAM);
    }

    if ((cmd->have_input == NULL) || (cmd->width <=0) || (cmd->height <=0)
            || (cmd->coding <= OMX_ON2_VIDEO_CodingAutoDetect)) {
        VPU_DEMO_LOG("Warning: missing needed parameters for vpu api demo\n");
    }

    if (cmd->disable_debug) {
        VPU_API_DEMO_DEBUG_DISABLE = 1;
    }

    if (cmd->have_input) {
        VPU_DEMO_LOG("input bitstream w: %d, h: %d, coding: %d(%s), path: %s\n",
            cmd->width, cmd->height, cmd->coding,
            cmd->codec_type == CODEC_DECODER ? "decode" : "encode",
            cmd->input_file);

        pInFile = fopen(cmd->input_file, "rb");
        if (pInFile == NULL) {
            VPU_DEMO_LOG("input file not exsist\n");
            DEMO_ERR_RET(ERROR_INVALID_PARAM);
        }
    } else {
        VPU_DEMO_LOG("please set input bitstream file\n");
        DEMO_ERR_RET(ERROR_INVALID_PARAM);
    }

    if (cmd->have_output) {
        VPU_DEMO_LOG("vpu api demo output file: %s\n",
            cmd->output_file);
        pOutFile = fopen(cmd->output_file, "wb");
        if (pOutFile == NULL) {
            VPU_DEMO_LOG("can not write output file\n");
            DEMO_ERR_RET(ERROR_INVALID_PARAM);
        }
        if (cmd->record_frames ==0)
            cmd->record_frames = 5;
    }

    fseek(pInFile, 0L, SEEK_END);
    fileSize = ftell(pInFile);
    fseek(pInFile, 0L, SEEK_SET);

    memset(&demoPkt, 0, sizeof(VideoPacket_t));
    pkt = &demoPkt;
    pkt->data = NULL;
    pkt->pts = VPU_API_NOPTS_VALUE;
    pkt->dts = VPU_API_NOPTS_VALUE;

    memset(&decOut, 0, sizeof(DecoderOut_t));
    pOut = &decOut;
    pOut->data = (RK_U8*)(malloc)(sizeof(VPU_FRAME));
    if (pOut->data ==NULL) {
        DEMO_ERR_RET(ERROR_MEMORY);
    }
    memset(pOut->data, 0, sizeof(VPU_FRAME));

    ret = vpu_open_context(&ctx);
    if (ret || (ctx ==NULL)) {
        DEMO_ERR_RET(ERROR_MEMORY);
    }

    /*
     ** read codec extra data from input stream file.
    */
    if (readBytesFromFile((RK_U8*)(&extraSize), 4, pInFile)) {
        DEMO_ERR_RET(ERROR_IO);
    }

    VPU_DEMO_LOG("codec extra data size: %d", extraSize);

    pExtra = (RK_U8*)(malloc)(extraSize);
    if (pExtra ==NULL) {
        DEMO_ERR_RET(ERROR_MEMORY);
    }
    memset(pExtra, 0, extraSize);

    if (readBytesFromFile(pExtra, extraSize, pInFile)) {
        DEMO_ERR_RET(ERROR_IO);
    }

    /*
     ** now init vpu api context. codecType, codingType, width ,height
     ** are all needed before init.
    */
    ctx->codecType = cmd->codec_type;
    ctx->videoCoding = cmd->coding;
    ctx->width = cmd->width;
    ctx->height = cmd->height;
    ctx->no_thread = 1;

    if ((ret = ctx->init(ctx, pExtra, extraSize)) !=0) {
       VPU_DEMO_LOG("init vpu api context fail, ret: 0x%X", ret);
       DEMO_ERR_RET(ERROR_INIT_VPU);
    }

    /*
     ** vpu api decoder process.
    */
    VPU_DEMO_LOG("init vpu api context ok, fileSize: %d", fileSize);

    do {
        if (ftell(pInFile) >=fileSize) {
           VPU_DEMO_LOG("read end of file, complete");
           break;
        }

        if (pkt && (pkt->size ==0)) {
            if (readBytesFromFile((RK_U8*)(&pkt_size), 4, pInFile)) {
                break;
            }

            if (pkt->data ==NULL) {
                pkt->data = (RK_U8*)(malloc)(pkt_size);
                if (pkt->data ==NULL) {
                    DEMO_ERR_RET(ERROR_MEMORY);
                }
                pkt->capability = pkt_size;
            }

            if (pkt->capability <((RK_U32)pkt_size)) {
                pkt->data = (RK_U8*)(realloc)((void*)(pkt->data), pkt_size);
                if (pkt->data ==NULL) {
                    DEMO_ERR_RET(ERROR_MEMORY);
                }
                pkt->capability = pkt_size;
            }

            if (readBytesFromFile(pkt->data, pkt_size, pInFile)) {
                break;
            } else {
                pkt->size = pkt_size;
                pkt->pts = fakeTimeUs;
                fakeTimeUs +=40000;
            }

            VPU_DEMO_LOG("read one packet, size: %d, pts: %lld, filePos: %ld",
                pkt->size, pkt->pts, ftell(pInFile));
        }

        /* note: must set out put size to 0 before do decoder. */
        pOut->size = 0;

        if ((ret = ctx->decode(ctx, pkt, pOut)) !=0) {
           DEMO_ERR_RET(ERROR_VPU_DECODE);
        } else {
            VPU_DEMO_LOG("vpu decode one frame, out len: %d, left size: %d",
                pOut->size, pkt->size);

            /*
             ** both virtual and physical address of the decoded frame are contained
             ** in structure named VPU_FRAME, if you want to use virtual address, make
             ** sure you have done VPUMemLink before.
            */
            if ((pOut->size) && (pOut->data)) {
                VPU_FRAME *frame = (VPU_FRAME *)(pOut->data);
                VPUMemLink(&frame->vpumem);
                RK_U32 wAlign16 = ((frame->DisplayWidth+ 15) & (~15));
                RK_U32 hAlign16 = ((frame->DisplayHeight + 15) & (~15));
                RK_U32 frameSize = wAlign16*hAlign16*3/2;

                if(pOutFile && (frame_count++ <cmd->record_frames)) {
                    VPU_DEMO_LOG("write %d frame(yuv420sp) data, %d bytes to file",
                        frame_count, frameSize);

                    fwrite((RK_U8*)(frame->vpumem.vir_addr), 1, frameSize, pOutFile);
                    fflush(pOutFile);
                }

                /*
                 ** remember use VPUFreeLinear to free, other wise memory leak will
                 ** give you a surprise.
                */
                VPUFreeLinear(&frame->vpumem);
                pOut->size = 0;
            }
        }

        usleep(30);
    }while(!(ctx->decoder_err));


DEMO_OUT:
    if (pkt && pkt->data) {
        free(pkt->data);
        pkt->data = NULL;
    }

    if (pOut && (pOut->data)) {
        free(pOut->data);
        pOut->data = NULL;
    }

    if (pExtra) {
        free(pExtra);
        pExtra = NULL;
    }

    if (ctx) {
        vpu_close_context(&ctx);
        ctx = NULL;
    }

    if (pInFile) {
        fclose(pInFile);
        pInFile = NULL;
    }
    if (pOutFile) {
        fclose(pOutFile);
        pOutFile = NULL;
    }

    if (ret) {
        VPU_DEMO_LOG("vpu api demo fail, err: %d", ret);
    } else {
        VPU_DEMO_LOG("vpu api demo complete OK.");
    }
    return ret;
}
