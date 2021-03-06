/* Goxel 3D voxels editor
 *
 * copyright (c) 2016 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Support for KVX format, used by the Build engine (Shadow Warrior/Blood)
 * and KV6 format, used by Voxlap, Evaldraw.
 * From the great Ken Silvemans.
 */

#include "goxel.h"

/*
 * Structure that represents a single voxel and visible faces.
 * Used as an intermediate structure for export.
 */
typedef struct {
    int pos[3];
    int color;
    int vis;
} voxel_t;

/*
 * Structure that represents a single slab, for export.
 */
typedef struct {
    int     pos[3];         // Starting top position.
    uint8_t len;            // Number of voxels in the slab.
    uint8_t vis;            // Visible faces.
    uint8_t colors[256];    // Colors from top to bottom.
} slab_t;


#define READ(type, file) \
    ({ type v; size_t r = fread(&v, sizeof(v), 1, file); (void)r; v;})
#define WRITE(type, v, file) \
    ({ type v_ = v; fwrite(&v_, sizeof(v_), 1, file);})

#define raise(msg) do { \
        LOG_E(msg); \
        ret = -1; \
        goto end; \
    } while (0)

static inline int AT(int x, int y, int z, int w, int h, int d) {
    y = h - y - 1;
    z = d - z - 1;
    return x + y * w + z * w * h;
}

static void swap_color(uint32_t v, uint8_t ret[4])
{
    uint8_t o[4];
    memcpy(o, &v, 4);
    ret[0] = o[2];
    ret[1] = o[1];
    ret[2] = o[0];
    ret[3] = o[3];
}

static int kv6_import(const char *path)
{
    FILE *file;
    char magic[4];
    int i, r, ret = 0, w, h, d, blklen, x, y, z = 0, nb, p = 0;
    uint32_t *xoffsets = NULL;
    uint16_t *xyoffsets = NULL;
    uint8_t (*cube)[4] = NULL;
    uint8_t color[4] = {0};
    (void)r;
    struct {
        uint32_t color;
        uint8_t zpos;
        uint8_t visface;
    } *blocks = NULL;

    path = path ?: noc_file_dialog_open(NOC_FILE_DIALOG_OPEN,
                                        "kv6\0*.kv6\0", NULL, NULL);
    if (!path) return -1;

    file = fopen(path, "rb");
    r = fread(magic, 1, 4, file);
    if (strncmp(magic, "Kvxl ", 4) != 0) raise("Invalid magic");
    w = READ(uint32_t, file);
    h = READ(uint32_t, file);
    d = READ(uint32_t, file);
    cube = calloc(w * h * d, sizeof(*cube));

    READ(float, file);
    READ(float, file);
    READ(float, file);
    blklen = READ(uint32_t, file);
    blocks = calloc(blklen, sizeof(*blocks));
    for (i = 0; i < blklen; i++) {
        blocks[i].color = READ(uint32_t, file);
        blocks[i].zpos = READ(uint16_t, file);
        blocks[i].visface = READ(uint8_t, file);
        READ(uint8_t, file); // lighting
    }
    xoffsets = calloc(w, sizeof(*xoffsets));
    xyoffsets = calloc(w * h, sizeof(*xyoffsets));
    for (i = 0; i < w; i++)      xoffsets[i] = READ(uint32_t, file);
    for (i = 0; i < w * h; i++) xyoffsets[i] = READ(uint16_t, file);

    for (x = 0; x < w; x++)
    for (y = 0; y < h; y++) {
        nb = xyoffsets[x * h + y];
        for (i = 0; i < nb; i++, p++) {
            z = blocks[p].zpos;
            swap_color(blocks[p].color, cube[AT(x, y, z, w, h, d)]);
        }
    }

    // Fill
    p = 0;
    for (x = 0; x < w; x++)
    for (y = 0; y < h; y++) {
        nb = xyoffsets[x * h + y];
        for (i = 0; i < nb; i++, p++) {
            if (blocks[p].visface & 0x10) {
                z = blocks[p].zpos;
                swap_color(blocks[p].color, color);
                color[3] = 255;
            }
            if (blocks[p].visface & 0x20) {
                for (; z < blocks[p].zpos; z++)
                    if (cube[AT(x, y, z, w, h, d)][3] == 0)
                        memcpy(cube[AT(x, y, z, w, h, d)], color, 4);
            }
        }
    }

    mesh_blit(goxel.image->active_layer->mesh, (const uint8_t*)cube,
              -w / 2, -h / 2, -d / 2, w, h, d, NULL);
    goxel_update_meshes(-1);
end:
    free(cube);
    free(blocks);
    free(xoffsets);
    free(xyoffsets);
    fclose(file);
    return ret;
}

static int kvx_import(const char *path)
{
    FILE *file;
    int i, r, ret = 0, nb, w, h, d, x, y, z, lastz = 0, len, visface;
    uint8_t color = 0;
    uint8_t (*palette)[4] = NULL;
    uint32_t *xoffsets = NULL;
    uint16_t *xyoffsets = NULL;
    uint8_t (*cube)[4] = NULL;
    long datpos;
    (void)r;

    path = path ?: noc_file_dialog_open(NOC_FILE_DIALOG_OPEN,
                                        "kvx\0*.kvx\0", NULL, NULL);
    if (!path) return -1;

    file = fopen(path, "rb");
    nb = READ(uint32_t, file); (void)nb;
    w = READ(uint32_t, file);
    h = READ(uint32_t, file);
    d = READ(uint32_t, file);
    cube = calloc(w * h * d, sizeof(*cube));

    READ(uint32_t, file);
    READ(uint32_t, file);
    READ(uint32_t, file);

    xoffsets = calloc(w + 1, sizeof(*xoffsets));
    xyoffsets = calloc(w * (h + 1), sizeof(*xyoffsets));
    for (i = 0; i < w + 1; i++)        xoffsets[i] = READ(uint32_t, file);
    for (i = 0; i < w * (h + 1); i++) xyoffsets[i] = READ(uint16_t, file);

    datpos = ftell(file);

    // Read the palette at the end of the file first.
    fseek(file, -256 * 3, SEEK_END);
    palette = calloc(256, sizeof(*palette));
    for (i = 0; i < 256; i++) {
        palette[i][0] = clamp(round(READ(uint8_t, file) * 255 / 63.f), 0, 255);
        palette[i][1] = clamp(round(READ(uint8_t, file) * 255 / 63.f), 0, 255);
        palette[i][2] = clamp(round(READ(uint8_t, file) * 255 / 63.f), 0, 255);
        palette[i][3] = 255;
    }
    fseek(file, datpos, SEEK_SET);

    for (x = 0; x < w; x++)
    for (y = 0; y < h; y++) {
        if (xyoffsets[x * (h + 1) + y + 1] < xyoffsets[x * (h + 1) + y])
            raise("Invalid format");
        nb = xyoffsets[x * (h + 1) + y + 1] - xyoffsets[x * (h + 1) + y];
        while (nb > 0) {
            z = READ(uint8_t, file);
            len = READ(uint8_t, file);
            visface = READ(uint8_t, file);
            assert(z + len - 1  < d);
            for (i = 0; i < len; i++) {
                color = READ(uint8_t, file);
                memcpy(cube[AT(x, y, z + i, w, h, d)], palette[color], 4);
            }
            nb -= len + 3;

            /* KVX format only saves the visible voxels.  Since we have the
             * face information, we can fill the gaps ourself between
             * top visible and bottom visible voxels.
             * Note: this should be an option.  */
            if (visface & 0x10) lastz = z + len;
            if (visface & 0x20) {
                for (i = lastz; i < z; i++) {
                    if (cube[AT(x, y, i, w, h, d)][3] == 0) {
                        memcpy(cube[AT(x, y, i, w, h, d)], palette[color], 4);
                    }
                }
            }
        }
    }

    bbox_from_aabb(goxel.image->box, (int[2][3]){{0, 0, 0}, {w, h, d}});
    bbox_from_aabb(goxel.image->active_layer->box,
                   (int[2][3]){{0, 0, 0}, {w, h, d}});
    mesh_blit(goxel.image->active_layer->mesh, (uint8_t*)cube,
              0, 0, 0, w, h, d, NULL);
    goxel_update_meshes(-1);

end:
    free(palette);
    free(cube);
    free(xoffsets);
    free(xyoffsets);
    fclose(file);
    return ret;
}


static int get_color_index(uint8_t v[4], uint8_t (*palette)[4])
{
    const uint8_t *c;
    int i, dist, best = -1, best_dist = 1024;
    for (i = 1; i < 256; i++) {
        c = palette[i];
        dist = abs((int)c[0] - (int)v[0]) +
               abs((int)c[1] - (int)v[1]) +
               abs((int)c[2] - (int)v[2]);
        if (dist == 0) return i;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

// Sort the voxels as they appear in the slabs.
static int voxel_cmp(const void *a_, const void *b_)
{
    const voxel_t *a = (void*)a_;
    const voxel_t *b = (void*)b_;
    return cmp(a->pos[0], b->pos[0]) ?:
           cmp(a->pos[1], b->pos[1]) ?:
           cmp(a->pos[2], b->pos[2]);
}

/*
 * Attempt to add a voxel into a slab, return true for success.
 */
static bool slab_append(slab_t *slab, voxel_t *vox)
{
    if (slab->vis & 32) return false; // Slab already finished.
    if (slab->len == 255) return false; // No more space.

    if (slab->len == 0) {
        memcpy(slab->pos, vox->pos, sizeof(slab->pos));
        slab->vis = vox->vis;
    }

    if (    vox->pos[0] != slab->pos[0] ||
            vox->pos[1] != slab->pos[1] ||
            vox->pos[2] != slab->pos[2] + slab->len)
        return false;

    // All the vertical faces should have the same visibility.
    if ((vox->vis & 15) != (slab->vis & 15)) return false;
    slab->vis |= (vox->vis & 32); // Add bottom face if needed.
    slab->colors[slab->len++] = vox->color;
    return true;
}


static void kvx_export(const mesh_t *mesh, const char *path)
{
    FILE *file;
    uint8_t (*palette)[4];
    mesh_iterator_t iter;
    mesh_accessor_t acc;
    uint8_t v[4];
    float box[4][4];
    int size[3], orig[3], x, y, i;
    UT_array *slabs;
    UT_array *voxels;
    slab_t *slab;
    voxel_t voxel, *vox;
    uint32_t ofs;
    uint32_t *xoffsets;
    uint32_t *xyoffsets;

    UT_icd voxel_icd = {sizeof(voxel_t), NULL, NULL, NULL};
    UT_icd slab_icd = {sizeof(slab_t), NULL, NULL, NULL};

    mat4_copy(goxel.image->box, box);
    if (box_is_null(box)) mesh_get_box(mesh, true, box);

    size[0] = box[0][0] * 2;
    size[1] = box[1][1] * 2;
    size[2] = box[2][2] * 2;
    orig[0] = box[3][0] - box[0][0];
    orig[1] = box[3][1] - box[1][1];
    orig[2] = box[3][2] - box[2][2];

    file = fopen(path, "wb");
    // Generates the palette.
    palette = calloc(256, sizeof(*palette));
    quantization_gen_palette(mesh, 256, (void*)(palette));

    // Iter the voxels and only keep the visible ones, plus the visible
    // faces mask.  Put them all into an array.
    utarray_new(voxels, &voxel_icd);
    iter = mesh_get_box_iterator(
            mesh, box, MESH_ITER_VOXELS | MESH_ITER_SKIP_EMPTY);
    acc = mesh_get_accessor(mesh);

    while (mesh_iter(&iter, voxel.pos)) {
        mesh_get_at(mesh, &iter, voxel.pos, v);
        if (v[3] < 127) continue;
        // Compute visible face mask.
        voxel.vis = 0;
        #define vis_test(x, y, z) \
            (mesh_get_alpha_at(mesh, &acc, (int[]){voxel.pos[0] + (x), \
                                                   voxel.pos[1] + (y), \
                                                   voxel.pos[2] + (z)}) < 127)
        if (vis_test(-1,  0,  0)) voxel.vis |= 1;
        if (vis_test(+1,  0,  0)) voxel.vis |= 2;
        if (vis_test( 0, +1,  0)) voxel.vis |= 4;
        if (vis_test( 0, -1,  0)) voxel.vis |= 8;
        if (vis_test( 0,  0, +1)) voxel.vis |= 16;
        if (vis_test( 0,  0, -1)) voxel.vis |= 32;

        #undef vis_test
        if (!voxel.vis) continue; // No visible faces.
        voxel.color = get_color_index(v, palette);
        voxel.pos[0] -= orig[0];
        voxel.pos[1] -= orig[1];
        voxel.pos[2] -= orig[2];

        voxel.pos[1] = size[1] - voxel.pos[1] - 1;
        voxel.pos[2] = size[2] - voxel.pos[2] - 1;

        assert(voxel.pos[0] >= 0 && voxel.pos[0] < size[0]);
        assert(voxel.pos[1] >= 0 && voxel.pos[1] < size[1]);
        assert(voxel.pos[2] >= 0 && voxel.pos[2] < size[2]);
        utarray_push_back(voxels, &voxel);
    }

    // Sort the voxels by xy columns in order they will be in the slabs.
    utarray_sort(voxels, voxel_cmp);

    // Iter the voxels and generates the slabs array.
    utarray_new(slabs, &slab_icd);
    utarray_extend_back(slabs); // Add an initial slab.
    for (vox = (void*)utarray_front(voxels); vox;
         vox = (void*)utarray_next(voxels, vox))
    {
        slab = (void*)utarray_back(slabs);
        if (slab_append(slab, vox)) continue;
        // Finished a slab, create a new one.
        utarray_extend_back(slabs); // Add a new slab.
        slab = (void*)utarray_back(slabs);
        slab_append(slab, vox);     // Always works.
    }

    // Compute xoffsets and xyoffsetx.
    // Can we do it in a simpler way?
    xoffsets = calloc(size[0] + 1, sizeof(*xoffsets));
    xyoffsets = calloc(size[0] * (size[1] + 1), sizeof(*xyoffsets));
    ofs = (size[0] + 1) * 4 + size[0] * (size[1] + 1) * 2;
    xoffsets[0] = ofs;

    ofs = (size[0] + 1) * 4 + size[0] * (size[1] + 1) * 2;
    slab = (void*)utarray_front(slabs);
    for (x = 0; x < size[0]; x++) {
        while (slab && slab->pos[0] <= x) {
            ofs += 3 + slab->len;
            slab = (void*)utarray_next(slabs, slab);
        }
        xoffsets[x + 1] = ofs;
    }

    ofs = (size[0] + 1) * 4 + size[0] * (size[1] + 1) * 2;
    slab = (void*)utarray_front(slabs);
    for (x = 0; x < size[0]; x++) {
        xyoffsets[x * (size[1] + 1)] = 0;
        for (y = 0; y < size[1]; y++) {
            while (slab && slab->pos[0] <= x && slab->pos[1] <= y) {
                ofs += 3 + slab->len;
                slab = (void*)utarray_next(slabs, slab);
            }
            xyoffsets[x * (size[1] + 1) + y + 1] = ofs - xoffsets[x];
        }
    }

    // Now we have all the data ready to be saved.
    WRITE(uint32_t, utarray_len(voxels), file);
    WRITE(uint32_t, size[0], file);
    WRITE(uint32_t, size[1], file);
    WRITE(uint32_t, size[2], file);
    WRITE(int32_t, size[0] / 2, file);
    WRITE(int32_t, size[1] / 2, file);
    WRITE(int32_t, size[2] / 2, file);

    for (i = 0; i < size[0] + 1; i++)
        WRITE(uint32_t, xoffsets[i], file);
    for (i = 0; i < size[0] * (size[1] + 1); i++)
        WRITE(uint16_t, xyoffsets[i], file);

    for (slab = (void*)utarray_front(slabs); slab;
         slab = (void*)utarray_next(slabs, slab))
    {
        assert(slab->pos[2] >= 0);
        assert(slab->pos[2] <= 255);
        assert(slab->pos[2] + slab->len - 1 < size[2]);
        WRITE(uint8_t, slab->pos[2], file);
        WRITE(uint8_t, slab->len, file);
        WRITE(uint8_t, slab->vis, file);
        fwrite(slab->colors, 1, slab->len, file);
    }

    for (i = 0; i < 256; i++) {
        WRITE(uint8_t, palette[i][0] / 4, file);
        WRITE(uint8_t, palette[i][1] / 4, file);
        WRITE(uint8_t, palette[i][2] / 4, file);
    }

    utarray_free(slabs);
    utarray_free(voxels);
    free(xoffsets);
    free(xyoffsets);
    free(palette);
    fclose(file);
}

static void export_as_kvx(const char *path)
{
    path = path ?: noc_file_dialog_open(NOC_FILE_DIALOG_SAVE,
                    "kvx\0*.kvx\0", NULL, "untitled.kvx");
    if (!path) return;
    kvx_export(goxel.layers_mesh, path);
}

ACTION_REGISTER(import_kv6,
    .help = "Import a slab kv6 image",
    .cfunc = kv6_import,
    .csig = "vp",
    .file_format = {
        .name = "kv6",
        .ext = "*.kv6\0"
    },
)

ACTION_REGISTER(import_kvx,
    .help = "Import a slab kvx image",
    .cfunc = kvx_import,
    .csig = "vp",
    .file_format = {
        .name = "kvx",
        .ext = "*.kvx\0"
    },
)

ACTION_REGISTER(export_as_kvx,
    .help = "Save the image as a slab kvx image",
    .cfunc = export_as_kvx,
    .csig = "vp",
    .file_format = {
        .name = "kvx",
        .ext = "*.kvx\0",
    },
)
