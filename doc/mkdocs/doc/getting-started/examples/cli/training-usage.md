## CPU Requirements
Training can be CPU intensive, but is expected to work for even laptop cpus for smaller workloads. The following tests are done using a Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz.

## Training Times

!!! note "Note"
    The times below are very approximate given runtime depends on the input and therefore may not be representative of exact runtimes but are intended to help guide file sizing for training.

It is recommended to train to completion, however the flag `--max-time-secs` will pause training and return the best result found within the time frame provided. Training to completion on a 100Mb file using 40 threads takes for the following stages:

Clustering Training: 480s
- The clustering trainer is not always able to use all the threads, typically able to fully utilize ~5-10 threads. Clustering training time scales linearly with file size, but is also dependent on many other factors.

ACE Training: 3000s
- Ace training time scales linearly with the file input size, and time spent is inversely proportional to the number of threads used. It is possible to decrease the time spent by tuning hyperparameters such as `maxGenerations` at the cost of worse results.

## Recommended usage
It is recommended to initially train/ test on a single smaller file to verify correctness. A very small subset of the data can typically be provided to the trainer to train compressors, such as 3 files out of 1000 files, however this depends on the properties of the dataset.
