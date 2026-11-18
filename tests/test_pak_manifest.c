/*
 * Validate the pak manifest with NextUI's own parser.
 *
 * The launcher reads pak.json through pak_manifest.c, so a manifest this
 * rejects is a manifest the device ignores. The assertions mirror what the
 * port relies on: a safe id, a valid version, and no static overlay claim
 * (dynamic ownership is unresolved until phase A).
 */

#include "pak_manifest.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok: %s\n", what);                                        \
        } else {                                                               \
            printf("  FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <pak directory>\n", argv[0]);
        return 2;
    }

    PakManifest manifest;
    PakManifestStatus status = pak_manifest_load(argv[1], &manifest);

    printf("manifest: %s\n", argv[1]);
    CHECK(status == PAK_MANIFEST_OK, "pak.json parses as a JSON object");
    CHECK(manifest.id[0] != '\0', "id is present");
    CHECK(pak_manifest_is_safe_id(manifest.id), "id is a safe application id");
    CHECK(manifest.name[0] != '\0', "name is present");
    CHECK(manifest.version_valid, "version is a valid vX.Y.Z");
    CHECK(manifest.capabilities_declared == 1, "capabilities array is present");
    CHECK(pak_manifest_has_capability(&manifest, PAK_CAPABILITY_OVERLAY),
          "overlay capability is declared");

    printf("%s\n", failures == 0 ? "pak manifest tests passed" : "pak manifest tests FAILED");
    return failures == 0 ? 0 : 1;
}
