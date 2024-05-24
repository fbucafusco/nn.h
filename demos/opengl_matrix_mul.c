#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <GL/glew.h>
#include <GL/freeglut.h>

// Initial dimensions of matrices
#define INIT_MATRIX_A_ROWS 400
#define INIT_MATRIX_A_COLS 200
#define INIT_MATRIX_B_ROWS 200
#define INIT_MATRIX_B_COLS 700

// Input and result matrices
float *matrixA;
float *matrixB;
float *matrixC;
float *matrixC_cpu;

const char *shader_source = R"(
#version 430 core
layout (local_size_x = 16, local_size_y = 16) in;

layout (std430, binding = 0) buffer MatrixABuffer {
    float elementsA[];
};

layout (std430, binding = 1) buffer MatrixBBuffer {
    float elementsB[];
};

layout (std430, binding = 2) buffer MatrixCBuffer {
    float elementsC[];
};

uniform int a_rows;
uniform int a_cols;
uniform int b_cols;

void main() 
{
    uint row = gl_GlobalInvocationID.x;
    uint col = gl_GlobalInvocationID.y;

    if (row < a_rows && col < b_cols) {
        float value = 0.0;
        for (int e = 0; e < a_cols; ++e) {
            value += elementsA[row * a_cols + e] * elementsB[e * b_cols + col];
        }
        elementsC[row * b_cols + col] = value;
    }
}
)";

GLuint create_shader(GLenum shaderType, const char *shaderSource)
{
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &shaderSource, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "ERROR::SHADER::COMPILATION_FAILED\n%s\n", infoLog);
    }

    return shader;
}

void multiply_matrixes(float *A, float *B, float *C, int aRows, int aCols, int bCols)
{
    for (int i = 0; i < aRows; ++i)
    {
        for (int j = 0; j < bCols; ++j)
        {
            C[i * bCols + j] = 0;
            for (int k = 0; k < aCols; ++k)
            {
                C[i * bCols + j] += A[i * aCols + k] * B[k * bCols + j];
            }
        }
    }
}

void multiply_matrixes_opengl(int aRows, int aCols, int bCols)
{
    GLuint computeShader = create_shader(GL_COMPUTE_SHADER, shader_source);
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, computeShader);
    glLinkProgram(shaderProgram);

    glUseProgram(shaderProgram);

    GLuint bufferA, bufferB, bufferC;
    glGenBuffers(1, &bufferA);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufferA);
    glBufferData(GL_SHADER_STORAGE_BUFFER, aRows * aCols * sizeof(float), matrixA, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bufferA);

    glGenBuffers(1, &bufferB);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufferB);
    glBufferData(GL_SHADER_STORAGE_BUFFER, aCols * bCols * sizeof(float), matrixB, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bufferB);

    glGenBuffers(1, &bufferC);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufferC);
    glBufferData(GL_SHADER_STORAGE_BUFFER, aRows * bCols * sizeof(float), NULL, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bufferC);

    glUniform1i(glGetUniformLocation(shaderProgram, "a_rows"), aRows);
    glUniform1i(glGetUniformLocation(shaderProgram, "a_cols"), aCols);
    glUniform1i(glGetUniformLocation(shaderProgram, "b_cols"), bCols);

    glDispatchCompute((aRows + 15) / 16, (bCols + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, aRows * bCols * sizeof(float), matrixC);

    glDeleteBuffers(1, &bufferA);
    glDeleteBuffers(1, &bufferB);
    glDeleteBuffers(1, &bufferC);
    glDeleteProgram(shaderProgram);
    glDeleteShader(computeShader);
}

void init_matrixes(int aRows, int aCols, int bCols)
{
    for (int i = 0; i < aRows * aCols; ++i)
    {
        matrixA[i] = (float)(rand() % 100);
    }
    for (int i = 0; i < aCols * bCols; ++i)
    {
        matrixB[i] = (float)(rand() % 100);
    }
}

bool compare_matrixes(float *C1, float *C2, int rows, int cols)
{
    for (int i = 0; i < rows; ++i)
    {
        for (int j = 0; j < cols; ++j)
        {
            if (fabs(C1[i * cols + j] - C2[i * cols + j]) > 1e-5)
            {
                return false;
            }
        }
    }
    return true;
}

void measure_time(struct timespec *start, struct timespec *end, const char *method, int aRows, int aCols, int bCols)
{
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, start);

    if (strcmp(method, "cpu") == 0)
    {
        multiply_matrixes(matrixA, matrixB, matrixC_cpu, aRows, aCols, bCols);
    }
    else if (strcmp(method, "gpu") == 0)
    {
        multiply_matrixes_opengl(aRows, aCols, bCols);
    }

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, end);
}

double time_diff(struct timespec *start, struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

int main(int argc, char **argv)
{
    const int matrix_sizes[][3] = {
        {2000, 6000, 1000},
        {1000, 6000, 100},
        {1000, 600, 1000},
        {600, 400, 1000},
        {400, 200, 700},
        {300, 150, 600},
        {200, 100, 500},
        {100, 50, 300},
        {50, 25, 200},
        {25, 12, 100}};

    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1, 1);
    glutCreateWindow("Matrix Multiplication using OpenGL Compute Shader");
    glewInit();

    for (int i = 0; i < sizeof(matrix_sizes) / sizeof(matrix_sizes[0]); ++i)
    {
        int aRows = matrix_sizes[i][0];
        int aCols = matrix_sizes[i][1];
        int bCols = matrix_sizes[i][2];

        matrixA = (float *)malloc(aRows * aCols * sizeof(float));
        matrixB = (float *)malloc(aCols * bCols * sizeof(float));
        matrixC = (float *)malloc(aRows * bCols * sizeof(float));
        matrixC_cpu = (float *)malloc(aRows * bCols * sizeof(float));

        init_matrixes(aRows, aCols, bCols);

        struct timespec start_user_cpu, end_user_cpu;
        measure_time(&start_user_cpu, &end_user_cpu, "cpu", aRows, aCols, bCols);

        double cpu_user_time = time_diff(&start_user_cpu, &end_user_cpu);

        struct timespec start_user_gpu, end_user_gpu;
        measure_time(&start_user_gpu, &end_user_gpu, "gpu", aRows, aCols, bCols);

        double gpu_user_time = time_diff(&start_user_gpu, &end_user_gpu);

#ifndef NO_PRINT_MAT
        printf("\nMatrix size: A[%d x %d], B[%d x %d]\n", aRows, aCols, aCols, bCols);
        printf("CPU User Time: %f seconds\n", cpu_user_time);
        printf("CPU System Time: %f seconds\n", cpu_sys_time);
        printf("GPU User Time: %f seconds\n", gpu_user_time);
        printf("GPU System Time: %f seconds\n", gpu_sys_time);
#endif

        if (compare_matrixes(matrixC, matrixC_cpu, aRows, bCols))
        {
        }
        else
        {
            printf("The matrices are different.\n");
        }

        int mulcount = aRows * aCols * bCols;
        int sumcount = aRows * aCols * (bCols - 1);

        if ((cpu_user_time) < (gpu_user_time))
        {
            printf("CPU is faster. %4u %4u %4u muls %10u sums %10u  = %f (%f)\n", aRows, aCols, bCols, mulcount, sumcount, cpu_user_time, gpu_user_time);
        }
        else
        {
            printf("GPU is faster. %4u %4u %4u muls %10u sums %10u = %f (%f)\n", aRows, aCols, bCols, mulcount, sumcount, gpu_user_time, cpu_user_time);
        }

        free(matrixA);
        free(matrixB);
        free(matrixC);
        free(matrixC_cpu);
    }

    return 0;
}
