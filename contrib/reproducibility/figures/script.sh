#!/usr/bin/env sh

# These variables need to be set correctly to work
export ZLI="../../../zli"
export TRACEOFF_PLOTS="./tradeoff-plots.py"
export LZBENCH="../lzbench/lzbench/lzbench"
export CORPUS="$HOME/openzl_corpus/"
export OUTPUT="$HOME/2026-01-22-openzl-whitepaper-pareto-optimal/"

mkdir -p "$OUTPUT"

./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "Binance" "$CORPUS/binance_canonical/train" "$CORPUS/binance_canonical/test" --output "$OUTPUT/binance" --profile parquet --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "TLC" "$CORPUS/tlc_canonical/train" "$CORPUS/tlc_canonical/test" --output "$OUTPUT/tlc" --profile parquet --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "PSAM-P" "$CORPUS/psam_p/train" "$CORPUS/psam_p/test" --output "$OUTPUT/psam-p" --profile csv --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "ERA5 Flux" "$CORPUS/era5_flux/train" "$CORPUS/era5_flux/test" --output "$OUTPUT/era5-flux" --profile le-u64 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "ERA5 Precip" "$CORPUS/era5_precip/train" "$CORPUS/era5_precip/test" --output "$OUTPUT/era5-precip" --profile le-u64 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "PPMF Unit" "$CORPUS/ppmf_unit/train" "$CORPUS/ppmf_unit/test" --output "$OUTPUT/ppmf-unit" --profile csv --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
# ./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "SAO" "$SAO" --output "$OUTPUT/sao-profile" --profile sao --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
# ./make-pareto-optimal-figures.py --lzbench "$LZBENCH" --title "SAO with SDDL" "$SAO" --output "$OUTPUT/sao-sddl" --profile sddl --profile-arg "$SAO_SDDL" --num-test-files 1 --num-train-files 1 --zli "$ZLI" --tradeoff-plots "$TRACEOFF_PLOTS"
