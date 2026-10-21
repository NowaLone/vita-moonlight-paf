#include <psp2/apputil.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

const char kLogPath[] = "ux0:data/moonlight/livearea-probe.log";
const char kRevPath[] = "ux0:data/moonlight/livearea-probe.rev";
const char kForcePath[] = "ux0:data/moonlight/livearea-probe.force-fallback";
const char kPayload[] = "ux0:data/moonlight/la";
const char kContents[] = "ux0:data/moonlight/la/sce_sys/livearea/contents";
const char kTitleId[] = "VLMP00001";
const unsigned int kExists = 0x80010011u;

const char *const kAssetNames[] = {
    "bg0.png",
    "startup.png",
    "frame1.png",
    "frame2.png",
    "frame3.png",
    "frame4.png",
    "frame5.png",
    "frame6.png",
    "frame7.png"
};

/* Eleven %d values, all the same revision: content-rev, frame1 rev, frame1 query,
 * frame1 label, frame2..frame6 rev, frame7 rev, frame7 label. */
const char kTemplate[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<livearea style=\"a5\" format-ver=\"01.00\" content-rev=\"%d\">\n"
    "  <livearea-background>\n"
    "    <image>bg0.png</image>\n"
    "  </livearea-background>\n"
    "  <gate>\n"
    "    <startup-image>startup.png</startup-image>\n"
    "  </gate>\n"
    "  <frame id=\"frame1\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"68\" x=\"0\" y=\"0\">frame1.png</image>\n"
    "      <target>psla:eboot?probe=1&amp;rev=%d</target>\n"
    "      <text align=\"center\" valign=\"bottom\" ellipsis=\"on\">\n"
    "        <str size=\"22\" color=\"#ffffff\" bold=\"on\">Test App %d</str>\n"
    "      </text>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "  <frame id=\"frame2\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"68\" x=\"0\" y=\"0\">frame2.png</image>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "  <frame id=\"frame3\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"68\" x=\"0\" y=\"0\">frame3.png</image>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "  <frame id=\"frame4\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"68\" x=\"0\" y=\"0\">frame4.png</image>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "  <frame id=\"frame5\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"68\" x=\"0\" y=\"0\">frame5.png</image>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "  <frame id=\"frame6\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"68\" x=\"0\" y=\"0\">frame6.png</image>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "  <frame id=\"frame7\" multi=\"o\" autoflip=\"0\" rev=\"%d\">\n"
    "    <liveitem default=\"on\">\n"
    "      <image align=\"center\" valign=\"center\" origin=\"frame\" width=\"286\" height=\"168\" x=\"0\" y=\"0\">frame7.png</image>\n"
    "      <target>psla:eboot</target>\n"
    "      <text align=\"center\" valign=\"bottom\" ellipsis=\"on\">\n"
    "        <str size=\"22\" color=\"#ffffff\" bold=\"on\">Test PC %d</str>\n"
    "      </text>\n"
    "    </liveitem>\n"
    "  </frame>\n"
    "</livearea>\n";

void probe_log(const char *fmt, ...)
{
    char line[512];
    va_list args;
    int length;

    va_start(args, fmt);
    length = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (length < 0) {
        return;
    }
    line[sizeof(line) - 1] = '\0';
    sceClibPrintf("%s\n", line);

    FILE *file = fopen(kLogPath, "a");
    if (file == NULL) {
        return;
    }
    fprintf(file, "%s\n", line);
    fclose(file);
}

void ensure_dir(const char *path)
{
    const int result = sceIoMkdir(path, 0777);
    if (result < 0 && (unsigned int)result != kExists) {
        probe_log("[LiveAreaProbe] mkdir %s result=0x%08x", path, (unsigned int)result);
    }
}

int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

int write_bytes(const char *path, const char *bytes, int length)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        probe_log("[LiveAreaProbe] open %s errno=%d", path, errno);
        return -1;
    }
    const size_t written = fwrite(bytes, 1, (size_t)length, file);
    const int flush_error = fclose(file) != 0;
    if (written != (size_t)length || flush_error) {
        probe_log("[LiveAreaProbe] write %s written=%u length=%d flush=%d errno=%d",
                  path, (unsigned int)written, length, flush_error, errno);
        return -1;
    }
    return 0;
}

int copy_file(const char *source, const char *destination)
{
    FILE *input = fopen(source, "rb");
    if (input == NULL) {
        probe_log("[LiveAreaProbe] copy open %s errno=%d", source, errno);
        return -1;
    }
    FILE *output = fopen(destination, "wb");
    if (output == NULL) {
        probe_log("[LiveAreaProbe] copy create %s errno=%d", destination, errno);
        fclose(input);
        return -1;
    }

    char buffer[1024];
    int result = 0;
    while (1) {
        const size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count > 0 && fwrite(buffer, 1, count, output) != count) {
            result = -1;
            break;
        }
        if (count < sizeof(buffer)) {
            if (ferror(input)) {
                result = -1;
            }
            break;
        }
    }
    fclose(input);
    if (fclose(output) != 0) {
        result = -1;
    }
    probe_log("[LiveAreaProbe] copy %s result=%d", destination, result);
    return result;
}

int format_template(char *xml, size_t xml_size, int rev)
{
    const int length = snprintf(xml, xml_size, kTemplate,
        rev, rev, rev, rev, rev, rev, rev, rev, rev, rev, rev);
    if (length < 0 || (size_t)length >= xml_size) {
        return -1;
    }
    return length;
}

int next_revision(void)
{
    int rev = 1;
    FILE *file = fopen(kRevPath, "r");
    if (file != NULL) {
        if (fscanf(file, "%d", &rev) != 1 || rev < 1 || rev > 1000000) {
            rev = 1;
        }
        fclose(file);
    }
    if (rev < 1) {
        rev = 1;
    }
    rev += 1;

    file = fopen(kRevPath, "w");
    if (file == NULL) {
        probe_log("[LiveAreaProbe] revision store errno=%d; using %d", errno, rev);
        return rev;
    }
    fprintf(file, "%d\n", rev);
    if (fclose(file) != 0) {
        probe_log("[LiveAreaProbe] revision close errno=%d; using %d", errno, rev);
    }
    return rev;
}

void log_launch_event(void)
{
    SceAppUtilAppEventParam event;
    char buffer[1024];
    memset(&event, 0, sizeof(event));
    memset(buffer, 0, sizeof(buffer));

    const int received = sceAppUtilReceiveAppEvent(&event);
    probe_log("[LiveAreaProbe] receive type=%u result=0x%08x",
              (unsigned int)event.type, (unsigned int)received);
    if (received < 0) {
        probe_log("[LiveAreaProbe] no launch event");
        return;
    }

    const int parsed = sceAppUtilAppEventParseLiveArea(&event, buffer);
    buffer[sizeof(buffer) - 1] = '\0';
    for (size_t index = 0; buffer[index] != '\0'; ++index) {
        const unsigned char value = (unsigned char)buffer[index];
        if (value < 0x20 || value > 0x7e) {
            buffer[index] = '?';
        }
    }
    probe_log("[LiveAreaProbe] parse result=0x%08x target=%s",
              (unsigned int)parsed, buffer[0] ? buffer : "<empty>");
}

int prepare_payload(int rev, char *xml, int length)
{
    const char *directories[] = {
        "ux0:data",
        "ux0:data/moonlight",
        kPayload,
        "ux0:data/moonlight/la/sce_sys",
        "ux0:data/moonlight/la/sce_sys/livearea",
        kContents
    };
    for (size_t index = 0; index < sizeof(directories) / sizeof(directories[0]); ++index) {
        ensure_dir(directories[index]);
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/template.xml", kContents);
    if (write_bytes(path, xml, length) != 0) {
        return -1;
    }
    probe_log("[LiveAreaProbe] template bytes=%d rev=%d path=%s", length, rev, path);

    int copied = 0;
    for (size_t index = 0; index < sizeof(kAssetNames) / sizeof(kAssetNames[0]); ++index) {
        char source[128];
        char destination[128];
        snprintf(source, sizeof(source), "app0:sce_sys/livearea/contents/%s", kAssetNames[index]);
        snprintf(destination, sizeof(destination), "%s/%s", kContents, kAssetNames[index]);
        if (copy_file(source, destination) == 0) {
            copied += 1;
        }
    }
    probe_log("[LiveAreaProbe] assets copied=%d/%u",
              copied, (unsigned int)(sizeof(kAssetNames) / sizeof(kAssetNames[0])));
    return 0;
}

int run_promoter(void)
{
    const int init = scePromoterUtilityInit();
    probe_log("[LiveAreaProbe] promoter init=0x%08x", (unsigned int)init);
    if (init < 0) {
        return 0;
    }

    ScePromoterUtilityLAUpdate args;
    memset(&args, 0, sizeof(args));
    strncpy(args.titleid, kTitleId, sizeof(args.titleid) - 1);
    strncpy(args.path, kPayload, sizeof(args.path) - 1);

    const int update = scePromoterUtilityUpdateLiveArea(&args);
    int state = -1;
    int state_call = -1;
    int waits = 0;
    if (update == 0) {
        for (; waits < 50; ++waits) {
            state_call = scePromoterUtilityGetState(&state);
            if (state_call < 0 || state == 0) {
                break;
            }
            sceKernelDelayThread(100 * 1000);
        }
    }
    int result = -1;
    const int result_call = scePromoterUtilityGetResult(&result);
    scePromoterUtilityExit();

    const int ok = update == 0 && result_call == 0 && result == 0 && state == 0;
    probe_log("[LiveAreaProbe] promoter update=0x%08x state_call=0x%08x state=%d waits=%d result_call=0x%08x result=0x%08x ok=%d path=%s",
              (unsigned int)update, (unsigned int)state_call, state, waits,
              (unsigned int)result_call, (unsigned int)result, ok, args.path);
    return ok;
}

void run_fallback(const char *xml, int length)
{
    const char *paths[] = {
        "ux0:app/VLMP00001/sce_sys/livearea/contents/template.xml",
        "ux0:appmeta/VLMP00001/livearea/contents/template.xml"
    };
    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        const int result = write_bytes(paths[index], xml, length);
        probe_log("[LiveAreaProbe] fallback %s result=%d", paths[index], result);
    }
}

}

extern "C" void livearea_probe_run(void)
{
    ensure_dir("ux0:data");
    ensure_dir("ux0:data/moonlight");
    probe_log("[LiveAreaProbe] begin");
    log_launch_event();

    char xml[4096];
    const int rev = next_revision();
    const int length = format_template(xml, sizeof(xml), rev);
    if (length < 0) {
        probe_log("[LiveAreaProbe] FAIL template format rev=%d", rev);
        return;
    }
    if (prepare_payload(rev, xml, length) != 0) {
        probe_log("[LiveAreaProbe] FAIL payload rev=%d", rev);
        return;
    }

    const int promoter_ok = run_promoter();
    const int force = file_exists(kForcePath);
    if (!promoter_ok || force) {
        probe_log("[LiveAreaProbe] fallback reason=%s",
                  promoter_ok ? "forced" : "promoter failed");
        run_fallback(xml, length);
    } else {
        probe_log("[LiveAreaProbe] fallback skipped; promoter reported success");
    }
    probe_log("[LiveAreaProbe] DONE rev=%d promoter_ok=%d force_fallback=%d",
              rev, promoter_ok, force);
    probe_log("[LiveAreaProbe] quit fully, reopen the bubble, expect Test PC %d / Test App %d",
              rev, rev);
}
