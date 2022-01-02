#!/usr/bin/env python3

# Copyright 2021 IBM Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# 2022-01-02: Modified by Jesse De Meulemeester.
#   - Made script more configurable to also accommodate for instance ARM results
#     - Which counters to filter e.g. outliers can be configured on the command
#       line
#     - Used NumPy instead of SQLAlchemy
#   - The output format is now based on a template
#   - Small formatting changes
#   - Added verbose flag to reduce number of printed messages

import argparse
import logging
import os
from string import Template
from typing import List, Optional, TextIO, Tuple

import numpy as np
from scipy import stats
from tqdm import tqdm


def parse_file(filename: str,
               constant_cols: Optional[List[int]] = None,
               nonzero_cols: Optional[List[int]] = None,
               outlier_cols: Optional[List[int]] = None
               ) -> Tuple[np.ndarray, np.ndarray]:
    """Parse the file and return the mean and std of each column

    @param filename The name of the file to parse
    @param constant_cols The columns for which a constant value is expected. Any
                         rows not containing this value will be discarded
    @param nonzero_cols The columns for which any rows containing zero should be
                        removed
    @param outlier_cols The columns for which any rows containing outliers
                        should be removed

    @return mean An array containing the means of each column
    @return std An array containing the standard deviations of each column
    """
    # Read the file into a numpy array
    data = np.genfromtxt(filename, dtype=np.uint64, delimiter="|",
                         skip_header=1)
    # Since the last character of each row contains a delimiter, the last column
    # has to be removed
    data = data[:, :-1]

    if constant_cols is not None:
        # Find the most occurring sequence
        values, counts = np.unique(data[:, constant_cols], return_counts=True)
        most_occurring = values[np.argmax(counts)]

        # Filter out any rows for which these rows are not constant
        row_mask = (data[:, constant_cols] == most_occurring).all(axis=1)
        data = data[row_mask]

        logging.debug("\tFiltered %d rows containing non-constant values",
                      np.count_nonzero(~row_mask))

    if nonzero_cols is not None:
        # Remove all rows for which the value in the columns specified by
        # `nonzero_cols' is zero
        row_mask = (data[:, nonzero_cols] != 0).all(axis=1)
        data = data[row_mask]

        logging.debug("\tFiltered %d rows containing zero values",
                      np.count_nonzero(~row_mask))

    if outlier_cols is not None:
        # Remove all rows that contain outliers in the columns described by
        # `outliers_cols'
        z_score = np.abs(stats.zscore(data[:, outlier_cols]))

        # Ignore columns where all elements are the same
        # These will result in a z_score equal to NaN
        z_score[np.isnan(z_score)] = 0

        # Filter out any rows for which the value in one of the specified
        # columns is an outlier, i.e. has a z score larger than 3
        row_mask = (z_score < 3).all(axis=1)
        data = data[row_mask]

        logging.debug("\tFiltered %d rows containing outliers",
                      np.count_nonzero(~row_mask))

    # Take the mean and std of each column
    mean = data.mean(axis=0)
    std = data.std(axis=0)

    return mean, std


def print_result(template: TextIO, output_file: TextIO, filename: str,
                 mean: np.ndarray, std: np.ndarray) -> None:
    """Print the mean and std to the output file

    @param template The template specifying how to format the results in the
                    output file
    @param output_file The file to output the results to
    @param filename The filename of the analyzed file
    @param mean The means of the analyzed file
    @param std The standard deviations of the analyzed file
    """
    # Creating the mapping
    mapping = dict(filename=filename)

    for i in range(len(mean)):
        mapping[f"mean_{i}"] = mean[i]
        mapping[f"std_{i}"] = std[i]

    # For each line in the template file, replace all identifiers in the
    # template and write the result to the output file
    template.seek(0)
    for line in template:
        t = Template(line)
        output_file.write(t.safe_substitute(mapping))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--location", "-l",
                        required=True,
                        help="Specify the result directory to be process")
    parser.add_argument("--template", "-t", type=str,
                        help="Path to the template file. If not specified, "
                             "the default template file, located at "
                             "$SPEC_H/scripts/templates/final_results.txt.in, "
                             "will be used.")
    parser.add_argument("--constant-cols", "-c", type=int, nargs="+",
                        help="The columns for which a constant value is "
                             "expected. Any rows not containing this value "
                             "will be discarded")
    parser.add_argument("--nonzero-cols", "-n", type=int, nargs="+",
                        help="The columns for which any rows containing zero "
                             "should be removed.")
    parser.add_argument("--outlier-cols", "-o", type=int, nargs="+",
                        help="The columns for which any rows containing "
                             "outliers should be removed.")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print debug output.")

    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(format="%(message)s", level=logging.DEBUG)

    if args.template is None:
        tmpl_filename = f"{os.environ['SPEC_H']}/scripts/templates" \
                            f"/final_results.txt.in "
    else:
        tmpl_filename = args.template

    final_filename = os.path.join(args.location, "final_results.txt")

    with open(final_filename, "w") as final, open(tmpl_filename, "r") as tmpl:
        for dirname, _, filenames in os.walk(args.location):
            for file in tqdm(sorted(filenames), disable=args.verbose):
                if file == "final_results.txt":
                    continue

                logging.debug("Considering %s", file)

                f = os.path.join(dirname, file)
                m, std = parse_file(f, args.constant_cols, args.nonzero_cols,
                                    args.outlier_cols)
                print_result(tmpl, final, file, m, std)


if __name__ == "__main__":
    main()
