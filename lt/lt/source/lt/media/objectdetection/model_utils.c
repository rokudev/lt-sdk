/*******************************************************************************
 * LT Object Detection Model Utilities
 *
 * Post-processing utilities for object detection models including mathematical
 * functions, tensor operations, non-maximum suppression, confidence filtering,
 * coordinate transformations, and inference result processing.
 *
 * This module provides optimized implementations of common post-processing
 * operations required for object detection pipelines, designed to work
 * efficiently on embedded systems with limited computational resources.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/media/objectdetection/model_utils.h>

// DEFINE_LTLOG_SECTION("LTMediaObjectDetection")


#define EXP_EPSILON     1e-7f   // Improved precision for convergence  
#define EXP_MAX_TERMS   100     // Prevent infinite loops with more precision
#define EXP_RANGE_LIMIT 20.0f   // Range limit for numerical stability

/* Optimized function to calculate e^x using Taylor Series with range reduction
 * e^x = 1 + x + (x^2)/2! + (x^3)/3! + (x^4)/4! + ... = Σ (x^n)/n!
 * Uses range reduction: e^x = e^(a+b) = e^a * e^b where a is integer, |b| < 1
 */
float fast_exp(float x) {
    // Handle special cases
    if (x == 0.0f) return 1.0f;
    if (x < -EXP_RANGE_LIMIT) return 0.0f;  // Underflow protection
    if (x > EXP_RANGE_LIMIT) return 1e10f;  // Overflow protection (large value)

    // Range reduction: split x into integer and fractional parts
    // e^x = e^(int_part + frac_part) = e^int_part * e^frac_part
    int int_part = (x >= 0.0f) ? (int)x : (int)x - 1;
    float frac_part = x - int_part;

    // Calculate e^frac_part using Taylor series (|frac_part| <= 1)
    float sum = 1.0f;    // Start with first term (x^0/0! = 1)
    float term = 1.0f;   // Current term value

    for (int n = 1; n < EXP_MAX_TERMS; n++) {
        term *= frac_part / n;  // More efficient: term = term * x / n
        sum += term;

        // Check convergence
        if (lt_fabs(term) < EXP_EPSILON) {
            break;
        }
    }

    // Calculate e^int_part by repeated multiplication (e^1 ≈ 2.718281828)
    float exp_int = 1.0f;
    const float e = 2.7182818284590452354f;  // More precise constant

    if (int_part > 0) {
        for (int i = 0; i < int_part; i++) {
            exp_int *= e;
        }
    } else if (int_part < 0) {
        for (int i = 0; i < -int_part; i++) {
            exp_int /= e;
        }
    }

    return exp_int * sum;
}

/* Simple sigmoid function using fast_exp: sigmoid(x) = 1 / (1 + e^(-x)) */
float sigmoid(float x) {
    return 1.0f / (1.0f + fast_exp(-x));
}

/* Softmax helper function for a single element */
float softmax(float x, float* input_array, int array_size) {
    // Subtract max value for numerical stability
    float max_val = input_array[0];
    for (int i = 1; i < array_size; i++) {
        if (input_array[i] > max_val) {
            max_val = input_array[i];
        }
    }

    // Calculate e^(x - max) for numerator
    float numerator = fast_exp(x - max_val);

    // Calculate sum of e^(xi - max) for denominator
    float denominator = 0.0f;
    for (int i = 0; i < array_size; i++) {
        denominator += fast_exp(input_array[i] - max_val);
    }

    return numerator / denominator;
}

/* Apply softmax normalization per row (batch processing)
 *
 * Performs standard softmax normalization on data arrays:
 * - Uses numerical stability technique (subtract max value)
 * - Normalizes values to sum to 1.0 for each row
 * - Processes multiple rows efficiently in batch
 *
 * Parameters:
 *   data: Input/output array of values [num_rows * num_cols]
 *   num_rows: Number of rows to process (e.g., batch size)
 *   num_cols: Number of columns per row (e.g., feature dimensions)
 *
 * After this function call, argument *data will contain normalized softmax values.
 *
 * Returns: 0 on success, -1 on error
 */
int softmax_per_line(float* data, int num_rows, int num_cols) {
    if (!data || num_rows <= 0 || num_cols <= 0) return -1;

    for (int i = 0; i < num_rows; i++) {
        float* row_data = &data[i * num_cols];

        // Find max for numerical stability
        float max_val = row_data[0];
        for (int j = 1; j < num_cols; j++) {
            if (row_data[j] > max_val) {
                max_val = row_data[j];
            }
        }

        // Apply exp and sum
        float sum = 0.0f;
        for (int j = 0; j < num_cols; j++) {
            row_data[j] = fast_exp(row_data[j] - max_val);
            sum += row_data[j];
        }

        // Normalize
        if (sum > 0.0f) {
            for (int j = 0; j < num_cols; j++) {
                row_data[j] /= sum;
            }
        }
    }
    return 0;
}
