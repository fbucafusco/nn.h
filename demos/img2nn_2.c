#include <assert.h>
#include <stdio.h>
#include <float.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <raylib.h>
#include <raymath.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "stb_image.h"
#include "stb_image_write.h"

#define GYM_IMPLEMENTATION
#include "gym.h"

#define NN_IMPLEMENTATION
#include "nn.h"

size_t arch[] = {3, 28, 28,  9, 1};

size_t max_epoch = 100 * 1000;
size_t batches_per_frame = 200;
size_t batch_size = 28;

float rate = 1.0f;
float scroll = 0.f;

bool paused = true;

char *args_shift(int *argc, char ***argv)
{
    assert(*argc > 0);
    char *result = **argv;
    (*argc) -= 1;
    (*argv) += 1;
    return result;
}

#define out_width 512
#define out_height 512
uint32_t out_pixels[out_width * out_height];
#define FPS 30
#define STR2(x) #x
#define STR(x) STR2(x)
#define READ_END 0
#define WRITE_END 1

void render_single_out_image(NN nn, float a)
{
    for (size_t i = 0; i < out_width * out_height; ++i)
    {
        out_pixels[i] = 0xFF000000;
    }

    size_t px, py, size;
    if (out_width > out_height)
    {
        size = out_height;
        px = out_width / 2 - size / 2;
        py = 0;
    }
    else
    {
        size = out_width;
        px = 0;
        py = out_height / 2 - size / 2;
    }

    ROW_AT(NN_INPUT(nn), 2) = a;
    gym_nn_image_grayscale(nn, &out_pixels[py * out_width + px], size, size, out_width, 0, 1);
}

int render_upscaled_video(NN nn, float duration, const char *out_file_path)
{
    int pipefd[2];

    if (pipe(pipefd) < 0)
    {
        fprintf(stderr, "ERROR: could not create a pipe: %s\n", strerror(errno));
        return 1;
    }

    pid_t child = fork();
    if (child < 0)
    {
        fprintf(stderr, "ERROR: could not fork a child: %s\n", strerror(errno));
        return 1;
    }

    if (child == 0)
    {
        if (dup2(pipefd[READ_END], STDIN_FILENO) < 0)
        {
            fprintf(stderr, "ERROR: could not reopen read end of pipe as stdin: %s\n", strerror(errno));
            return 1;
        }
        close(pipefd[WRITE_END]);

        int ret = execlp("ffmpeg",
                         "ffmpeg",
                         "-loglevel", "verbose",
                         "-y",
                         "-f", "rawvideo",
                         "-pix_fmt", "rgba",
                         "-s", STR(out_width) "x" STR(out_height),
                         "-r", STR(FPS),
                         "-an",
                         "-i", "-",
                         "-c:v", "libx264",
                         out_file_path,
                         NULL);
        if (ret < 0)
        {
            fprintf(stderr, "ERROR: could not run ffmpeg as a child process: %s\n", strerror(errno));
            return 1;
        }
        assert(0 && "unreachable");
    }

    close(pipefd[READ_END]);

    typedef struct
    {
        float start;
        float end;
    } Segment;

    Segment segments[] = {
        {0, 0},
        {0, 1},
        {1, 1},
        {1, 0},
    };
    size_t segments_count = ARRAY_LEN(segments);
    float segment_length = 1.0f / segments_count;

    size_t frame_count = FPS * duration;

    for (size_t i = 0; i < frame_count; ++i)
    {
        float a = ((float)i) / frame_count;
        size_t segment_index = floorf(a / segment_length);
        float segment_progress = a / segment_length - segment_index;
        if (segment_index > segments_count)
            segment_index = segment_length - 1;
        Segment segment = segments[segment_index];
        float b = segment.start + (segment.end - segment.start) * sqrtf(segment_progress);
        render_single_out_image(nn, b);
        write(pipefd[WRITE_END], out_pixels, sizeof(*out_pixels) * out_width * out_height);
        printf("a = %f, index = %zu, progress = %f, b = %f\n", a, segment_index, segment_progress, b);
    }

    close(pipefd[WRITE_END]);
    wait(NULL);
    printf("Generated %s!\n", out_file_path);
    return 0;
}

int render_upscaled_screenshot(NN nn, const char *out_file_path)
{
    render_single_out_image(nn, scroll);

    if (!stbi_write_png(out_file_path, out_width, out_height, 4, out_pixels, out_width * sizeof(*out_pixels)))
    {
        fprintf(stderr, "ERROR: could not save image %s\n", out_file_path);
        return 1;
    }

    printf("Generated %s\n", out_file_path);
    return 0;
}

typedef enum
{
    GHA_LEFT,
    GHA_RIGHT,
    GHA_CENTER,
} Gym_Horz_Align;

typedef enum
{
    GVA_TOP,
    GVA_BOTTOM,
    GVA_CENTER,
} Gym_Vert_Align;

Vector2 render_texture_in_slot(Texture2D texture, Gym_Horz_Align ha, Gym_Vert_Align va, Gym_Rect r)
{
    Vector2 position = {r.x, r.y};
    float scale = 0;
    if (r.w > r.h)
    {
        scale = r.h / texture.height;
        switch (ha)
        {
        case GHA_LEFT:
            break;
        case GHA_RIGHT:
            position.x += r.w;
            position.x -= texture.width * scale;
            break;
        case GHA_CENTER:
            position.x += r.w / 2;
            position.x -= texture.width * scale / 2;
            break;
        }
        DrawTextureEx(texture, position, 0, scale, WHITE);
    }
    else
    {
        scale = r.w / texture.width;
        switch (va)
        {
        case GVA_TOP:
            break;
        case GVA_BOTTOM:
            position.y += r.h;
            position.y -= texture.height * scale;
            break;
        case GVA_CENTER:
            position.y += r.h / 2;
            position.y -= texture.height * scale / 2;
            break;
        }
        DrawTextureEx(texture, position, 0, scale, WHITE);
    }

    Vector2 mouse_position = GetMousePosition();
    Rectangle hitbox = {
        position.x,
        position.y,
        texture.width * scale,
        texture.height * scale,
    };
    return CLITERAL(Vector2){
        (mouse_position.x - position.x) / hitbox.width,
        (mouse_position.y - position.y) / hitbox.height};
}

/* build training data int t for N images
   prepare the training data from the data set:
       1st col: normalized x      feeds inout NN
       2nd col: normalized y      feeds inout NN
       3rd col: which image       feeds inout NN
       4th col: expected output   expected output, used for input for back prop

       each row, pixels for each img, sorted as is in the originals
*/
Mat build_training_data(NN nn, uint8_t **images_pxs, int *widths, int *heights, size_t img_count)
{
    size_t pixel_count = 0;
    for (size_t i = 0; i < img_count; i++)
    {
        pixel_count += widths[i] * heights[i];
    }

    Mat t = mat_alloc(NULL, pixel_count, NN_INPUT(nn).cols + NN_OUTPUT(nn).cols);

    size_t offset = 0;

    /* fill info */
    for (size_t i = 0; i < img_count; i++)
    {
        uint8_t *image_pxs = images_pxs[i];

        for (int y = 0; y < heights[i]; ++y)
        {
            for (int x = 0; x < widths[i]; ++x)
            {
                size_t px_idx = y * widths[i] + x;
                size_t mat_idx = offset + px_idx;
                MAT_AT(t, mat_idx, 0) = (float)x / (widths[i] - 1);
                MAT_AT(t, mat_idx, 1) = (float)y / (heights[i] - 1);
                MAT_AT(t, mat_idx, 2) = (float)i;
                MAT_AT(t, mat_idx, 3) = image_pxs[px_idx] / 255.f;
            }
        }

        offset += widths[i] * heights[i];
    }

    return t;
}

void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s <image1> <image2> <image3> ...\n", program);
    fprintf(stderr, "       at least 2 images are required\n");
}

int main(int argc, char **argv)
{
    Region temp = region_alloc_alloc(256 * 1024 * 1024);

    const char *program = args_shift(&argc, &argv);

    if (argc < 2)
    {
        fprintf(stderr, "ERROR: no image is provided\n");
        print_usage(program);
        return 1;
    }

    int i = 0;

    size_t img_count = argc;
    uint8_t *img_pixels[img_count];
    int img_width[img_count];
    int img_height[img_count];
    int img_comp[img_count];

    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    printf("Current path: %s\n", cwd);

    while (argc > 0)
    {
        const char *img_file_path = args_shift(&argc, &argv);
        img_pixels[i] = (uint8_t *)stbi_load(img_file_path, &img_width[i], &img_height[i], &img_comp[i], 0);
        printf("Image: %s\n", img_file_path);

        if (img_pixels[i] == NULL)
        {
            fprintf(stderr, "ERROR: could not read image %s\n", img_file_path);
            return 1;
        }

        if (img_comp[i] != 1)
        {
            fprintf(stderr, "ERROR: %s is %d bits image. Only 8 bit grayscale images are supported\n", img_file_path, img_comp[i] * 8);
            return 1;
        }

        printf("%s size %dx%d %d bits\n", img_file_path, img_width[i], img_height[i], img_comp[i] * 8);

        i++;
    }

    NN nn = nn_alloc(NULL, arch, ARRAY_LEN(arch)); // instances the NN
    nn_rand(nn, -1, 1);                            // fill nn with random values

    Mat t = build_training_data(nn, img_pixels, img_width, img_height, img_count);

    size_t preview_width = 28;
    size_t preview_height = 28;
    size_t WINDOW_FACTOR = 80;
    size_t WINDOW_WIDTH = (16 * WINDOW_FACTOR);
    size_t WINDOW_HEIGHT = (9 * WINDOW_FACTOR);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "gym");
    SetTargetFPS(60);

    Gym_Plot plot = {0};
    Font font = LoadFontEx("./fonts/iosevka-regular.ttf", 72, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    Image preview_image[img_count];
    Texture2D preview_texture[img_count];

    /* generates previews with all black pixels*/
    for (int i = 0; i < img_count; i++)
    {
        preview_image[i] = GenImageColor(preview_width, preview_height, BLACK);
        preview_texture[i] = LoadTextureFromImage(preview_image[i]);
    }

    Image preview_scrolled = GenImageColor(preview_width, preview_height, BLACK);
    Texture2D preview_texture3 = LoadTextureFromImage(preview_scrolled);

    Image original_image[img_count];
    Texture2D oiginal_texture[img_count];

    /* previews for the original images */
    for (int i = 0; i < img_count; i++)
    {
        original_image[i] = GenImageColor(img_width[i], img_height[i], GRAY);
        for (size_t y = 0; y < (size_t)img_height[i]; ++y)
        {
            for (size_t x = 0; x < (size_t)img_width[i]; ++x)
            {
                uint8_t pixel = img_pixels[i][y * img_width[i] + x];
                ImageDrawPixel(&original_image[i], x, y, CLITERAL(Color){pixel, pixel, pixel, 255});
            }
        }

        oiginal_texture[i] = LoadTextureFromImage(original_image[i]);
    }

    Batch batch = {0};
    bool rate_dragging = false;
    bool scroll_dragging = false;
    size_t epoch = 0;

    int w = GetRenderWidth();
    int h = GetRenderHeight();

    /*images/preview space: will be matrix of NxN deendin on the number of the input images */
    int n_samples_x = ceil(sqrt(img_count));
    int n_samples_y = ceil(img_count / (float)n_samples_x);

    bool original_imgs_already_rendered = false;

    Gym_Rect r;
    r.w = w;
    r.h = h * 2 / 3;
    r.x = 0;
    r.y = h / 2 - r.h / 2;

    /* precalc all drawing regions because they are all the same on each iteration */
    int global_i = 0;
    Gym_Rect gym_plot_slot;
    Gym_Rect gym_nn_weights_heatmap_slot;
    Gym_Rect master_peview_slot;

    Gym_Rect images_slot;
    Gym_Rect preview_slot;
    Gym_Rect preview_slide_slot;
    Gym_Rect images_sub_slot[img_count];
    Gym_Rect previews_sub_slot[img_count];

    gym_layout_begin(GLO_HORZ, r, 3, 10);
    {
        gym_plot_slot = gym_layout_slot();
        gym_nn_weights_heatmap_slot = gym_layout_slot();
        master_peview_slot = gym_layout_slot();
        gym_layout_begin(GLO_VERT, master_peview_slot, 3, 0);
        {
            images_slot = gym_layout_slot();
            preview_slot = gym_layout_slot();
            preview_slide_slot = gym_layout_slot();

            gym_layout_begin(GLO_VERT, images_slot, n_samples_y, 0);
            {
                global_i = 0;

                for (int j = 0; j < n_samples_y; j++)
                {
                    Gym_Rect row_slot = gym_layout_slot();

                    gym_layout_begin(GLO_HORZ, row_slot, n_samples_x, 0);
                    {
                        for (int i = 0; i < n_samples_x && global_i < img_count; i++)
                        {
                            images_sub_slot[global_i] = gym_layout_slot();
                            global_i++;
                        }
                    }
                    gym_layout_end();
                }
                /* purge the non used laouts */
                for (int j = 0; j < img_count - global_i; j++)
                {
                    gym_layout_slot();
                }
            }
            gym_layout_end();
            /* TODO repetead code, can be generalized. */
            gym_layout_begin(GLO_VERT, preview_slot, n_samples_y, 0);
            {
                global_i = 0;

                for (int j = 0; j < n_samples_y; j++)
                {
                    Gym_Rect row_slot = gym_layout_slot();

                    gym_layout_begin(GLO_HORZ, row_slot, n_samples_x, 0);
                    {
                        for (int i = 0; i < n_samples_x && global_i < img_count; i++)
                        {
                            previews_sub_slot[global_i] = gym_layout_slot();
                            global_i++;
                        }
                    }
                    gym_layout_end();
                }
                /* purge the non used laouts */
                for (int j = 0; j < img_count - global_i; j++)
                {
                    gym_layout_slot();
                }
            }
            gym_layout_end();
        }
        gym_layout_end();
    }
    gym_layout_end();

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_SPACE))
        {
            paused = !paused;
        }
        if (IsKeyPressed(KEY_R))
        {
            epoch = 0;
            nn_rand(nn, -1, 1);
            plot.count = 0;
        }
        if (IsKeyPressed(KEY_S))
        {
            render_upscaled_screenshot(nn, "upscaled.png");
        }
        if (IsKeyPressed(KEY_X))
        {
            render_upscaled_video(nn, 5, "upscaled.mp4");
        }
        if (IsKeyPressed(KEY_P))
        {
            nn_print(nn, "nice");
        }

        for (size_t i = 0; i < batches_per_frame && !paused && epoch < max_epoch; ++i)
        {
            batch_process(&temp, &batch, batch_size, nn, t, rate);

            if (batch.finished)
            {
                epoch += 1;
                da_append(&plot, batch.cost);
                mat_shuffle_rows(t);
            }
        }

        /* exites the NN or each input and generat the previed for them.*/
        for (int i = 0; i < img_count; i++)
        {
            ROW_AT(NN_INPUT(nn), 2) = i;
            gym_nn_image_grayscale(nn, preview_image[i].data, preview_image[i].width, preview_image[i].height, preview_image[i].width, 0, 1);
            UpdateTexture(preview_texture[i], preview_image[i].data);
        }

        /* generates the preview for the scrolled input */
        ROW_AT(NN_INPUT(nn), 2) = scroll * (img_count - 1);
        gym_nn_image_grayscale(nn, preview_scrolled.data, preview_scrolled.width, preview_scrolled.height, preview_scrolled.width, 0, 1);
        UpdateTexture(preview_texture3, preview_scrolled.data);

        BeginDrawing();
        ClearBackground(GYM_BACKGROUND);
        {
            gym_plot(plot, gym_plot_slot, RED);

            gym_render_nn_weights_heatmap(nn, gym_nn_weights_heatmap_slot);

            /* input images */

            // if (!original_imgs_already_rendered)
            {
                /* input images slots */
                for (int i = 0; i < img_count; i++)
                {
                    render_texture_in_slot(oiginal_texture[i], GHA_CENTER, GVA_CENTER, images_sub_slot[i]);
                }

                original_imgs_already_rendered = true;
            }

            /* preview  images slots */
            for (int i = 0; i < img_count; i++)
            {
                render_texture_in_slot(preview_texture[i], GHA_CENTER, GVA_CENTER, previews_sub_slot[i]);
            }

            render_texture_in_slot(preview_texture3, GHA_CENTER, GVA_CENTER, preview_slide_slot);
            {
                float rw = master_peview_slot.w;
                float rh = master_peview_slot.h * 0.03;
                float rx = master_peview_slot.x;
                float ry = rh + master_peview_slot.y + master_peview_slot.h;
                gym_slider(&scroll, &scroll_dragging, rx, ry, rw, rh);
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Epoch: %zu/%zu, Rate: %f, Cost: %f, Temporary Memory: %zu\n", epoch, max_epoch, rate, plot.count > 0 ? plot.items[plot.count - 1] : 0, region_occupied_bytes(&temp));
            DrawTextEx(font, buffer, CLITERAL(Vector2){}, h * 0.04, 0, WHITE);
            gym_slider(&rate, &rate_dragging, 0, h * 0.08, w, h * 0.02);
        }
        EndDrawing();

        region_reset(&temp);
    }

    return 0;
}
