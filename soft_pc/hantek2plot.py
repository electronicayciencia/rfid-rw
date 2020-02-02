#!/usr/bin/env python3

"""
Reinoso G. - Blog Electronicayciencia - 02/02/2020

Hantek format:
---------
# CHANNEL:CH1
# CLOCK=20.0uS
# SIZE=130048
# UNITS:V

-1.5686
...
0.9412


# CHANNEL:CH2
# CLOCK=20.0uS
# SIZE=130048
# UNITS:V

2.6353
2.6353
2.5725
2.5725


---------
"""


def readHantek(filename):
    """
    Read a Hantek data format into a Pandas dictionary of data.
    Default timebase is 20uS, it should read it from file.

    Input
        filename: data file

    Output
        dictionary with all the data (t, CH1, [CH2])
    """
    timebase = 20e-6

    data = {}
    data["t"] = []
    chname = ""
    with open(filename) as fh:
        for line in fh:
            line = line.strip()

            if not line:
                continue

            if line.find("#CHANNEL") == 0:
                (_, chname) = line.split(":")
                data[chname] = []

            if line[0] == "#":
                continue

            if not chname:
                continue

            data[chname].append(float(line))

    # Fill the timeline from num elements in the last key
    for i in range(len(data[chname])):
        data["t"].append(timebase * i)

    return data


if __name__ == "__main__":
    import csv

    data = readHantek("../mod_field_data.rfc")
#    data = readHantek("testfile.txt")

    for i in range(len(data["t"])):
        print("%f,%f,%f" % (
            data["t"][i],
            data["CH1"][i],
            data["CH2"][i]
        ))
