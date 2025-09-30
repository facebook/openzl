#!/usr/bin/env sh

# These variables need to be set correctly to work
export ZLI="../../zli"
export TRACEOFF_PLOTS="./tradeoff-plots.py"
export LZBENCH="../lzbench/lzbench/lzbench"
export CORPUS="$HOME/datasets/openzl-corpus/"
export OUTPUT="$HOME/keep/2025-09-22-openzl-whitepaper-pareto-optimal/"
export SAO="$HOME/datasets/silesia/sao"
export SAO_SDDL="./artifact/sao-sddl/sao.sddl"

mkdir -p "$OUTPUT"

./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "Binance" "$CORPUS/binance-canonical/BTC-USDT.parquet" --output "$OUTPUT/binance" --profile parquet --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "ERA5 Flux" "$CORPUS/era5_flux" --output "$OUTPUT/era5-flux" --profile le-u64 --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "ERA5 Precip" "$CORPUS/era5_precip" --output "$OUTPUT/era5-precip" --profile le-u64 --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "PPMF Unit" "$CORPUS/ppmf_unit/unit_59.csv" --output "$OUTPUT/ppmf-unit" --profile csv --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "SAO" "$SAO" --output "$OUTPUT/sao-profile" --profile sao --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "SAO with SDDL" "$SAO" --output "$OUTPUT/sao-sddl" --profile sddl --profile-arg "$SAO_SDDL" --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "TLC Green Trip" "$CORPUS/tlc-canonical/green/green_tripdata_2025-01.parquet" --output "$OUTPUT/tlc-green-trip" --profile parquet --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
