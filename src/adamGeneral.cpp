#include <RcppArmadillo.h>
#include <iostream>
#include <cmath>
#include "ssGeneral.h"
#include "adamGeneral.h"
// [[Rcpp::depends(RcppArmadillo)]]

using namespace Rcpp;

// # Fitter for univariate models
List adamFitter(arma::mat &matrixVt, arma::mat const &matrixWt, arma::mat const &matrixF, arma::vec const &vectorG,
                arma::uvec &lags, char const &E, char const &T, char const &S,
                unsigned int const &nNonSeasonal, unsigned int const &nSeasonal,
                unsigned int const &nArima, unsigned int const &nXreg,
                arma::vec const &vectorYt, arma::vec const &vectorOt, bool const &backcast){
    /* # matrixVt should have a length of obs + lagsModelMax.
     * # matrixWt is a matrix with nrows = obs
     * # vecG should be a vector
     * # lags is a vector of lags
     */

    int obs = vectorYt.n_rows;
    int obsall = matrixVt.n_cols;
    unsigned int nETS = nNonSeasonal + nSeasonal;
    int nComponents = matrixVt.n_rows;
    arma::uvec lagsModifier = lags;
    arma::uvec lagsInternal = lags;
    int lagsModelMax = max(lagsInternal);
    int lagslength = lagsInternal.n_rows;

    lagsInternal = lagsInternal * nComponents;

    for(int i=0; i<lagslength; i=i+1){
        lagsModifier(i) = lagslength - i - 1;
    }

    arma::uvec lagrows(lagslength, arma::fill::zeros);

    // Fitted values and the residuals
    arma::vec vecYfit(obs, arma::fill::zeros);
    arma::vec vecErrors(obs, arma::fill::zeros);

    // Loop for the backcasting
    unsigned int nIterations = 2;
    if(backcast){
        nIterations = 3;
    }

    // Loop for the backcast
    for (unsigned int j=1; j<nIterations; j=j+1) {

        ////// Run forward
        // Loop for the model construction
        for (int i=lagsModelMax; i<obs+lagsModelMax; i=i+1) {
            lagrows = i * nComponents - (lagsInternal + lagsModifier) + nComponents - 1;

            /* # Measurement equation and the error term */
            vecYfit(i-lagsModelMax) = adamWvalue(matrixVt(lagrows), matrixWt.row(i-lagsModelMax), E, T, S,
                    nETS, nNonSeasonal, nSeasonal, nArima, nXreg, nComponents);

            // Failsafe for fitted becoming Infinite
            // if((E=='M') && !vecYfit.row(i-lagsModelMax).is_finite()){
            //     vecYfit(i-lagsModelMax) = 1E+300;
            // }

            // If this is zero (intermittent), then set error to zero
            if(vectorOt(i-lagsModelMax)==0){
                vecErrors(i-lagsModelMax) = 0;
            }
            else{
                vecErrors(i-lagsModelMax) = errorf(vectorYt(i-lagsModelMax), vecYfit(i-lagsModelMax), E);
            }

            /* # Transition equation */
            matrixVt.col(i) = adamFvalue(matrixVt(lagrows), matrixF, E, T, S, nETS, nNonSeasonal, nSeasonal, nArima, nComponents) +
            adamGvalue(matrixVt(lagrows), matrixF, matrixWt.row(i-lagsModelMax), E, T, S,
                   nETS, nNonSeasonal, nSeasonal, nArima, nXreg, nComponents, vectorG, vecErrors(i-lagsModelMax));

            // Failsafe for cases, when nan values appear
            if(matrixVt.col(i).has_nan()){
                matrixVt.col(i) = matrixVt(lagrows);
            }
            /* Failsafe for cases when unreasonable value for state vector was produced */
            // if(!matrixVt.col(i).is_finite()){
            //     matrixVt.col(i) = matrixVt(lagrows);
            // }
            if(E=='M' && (matrixVt(0,i) <= 0)){
                matrixVt(0,i) = 0.01;
            }

            /* Renormalise components if the seasonal model is chosen */
            // if(S!='N'){
            //     if(double(i+1) / double(lagsModelMax) == double((i+1) / lagsModelMax)){
            //         matrixVt.cols(i-lagsModelMax+1,i) = normaliser(matrixVt.cols(i-lagsModelMax+1,i), obsall, lagsModelMax, S, T);
            //     }
            // }
        }

        ////// Backwards run
        if(backcast && j<(nIterations-1)){
            // Fill in the tail of the series - this is needed for backcasting
            for (int i=obs+lagsModelMax; i<obsall; i=i+1) {
                lagrows = i * nComponents - (lagsInternal + lagsModifier) + nComponents - 1;
                matrixVt.col(i) = adamFvalue(matrixVt(lagrows), matrixF, E, T, S, nETS, nNonSeasonal, nSeasonal, nArima, nComponents);

                // /* Failsafe for cases when unreasonable value for state vector was produced */
                // if(!matrixVt.col(i).is_finite()){
                //     matrixVt.col(i) = matrixVt(lagrows);
                // }
                if((S=='M') && (matrixVt(nNonSeasonal,i) <= 0)){
                    matrixVt(nNonSeasonal,i) = arma::as_scalar(matrixVt(lagrows.row(nNonSeasonal)));
                }
                if(T=='M'){
                    if((matrixVt(0,i) <= 0) | (matrixVt(1,i) <= 0)){
                        matrixVt(0,i) = arma::as_scalar(matrixVt(lagrows.row(0)));
                        matrixVt(1,i) = arma::as_scalar(matrixVt(lagrows.row(1)));
                    }
                }
            }

            for (int i=obs+lagsModelMax-1; i>=lagsModelMax; i=i-1) {
                lagrows = i * nComponents + lagsInternal - lagsModifier + nComponents - 1;

                /* # Measurement equation and the error term */
                vecYfit(i-lagsModelMax) = adamWvalue(matrixVt(lagrows), matrixWt.row(i-lagsModelMax), E, T, S,
                        nETS, nNonSeasonal, nSeasonal, nArima, nXreg, nComponents);

                // Failsafe for fitted becoming negative in mixed models
                // if((E=='M') && (vecYfit(i-lagsModelMax)<0)){
                //     vecYfit(i-lagsModelMax) = 0.01;
                // }

                // If this is zero (intermittent), then set error to zero
                if(vectorOt(i-lagsModelMax)==0){
                    vecErrors(i-lagsModelMax) = 0;
                }
                else{
                    vecErrors(i-lagsModelMax) = errorf(vectorYt(i-lagsModelMax), vecYfit(i-lagsModelMax), E);
                }

                /* # Transition equation */
                matrixVt.col(i) = adamFvalue(matrixVt(lagrows), matrixF, E, T, S, nETS, nNonSeasonal, nSeasonal, nArima, nComponents) +
                adamGvalue(matrixVt(lagrows), matrixF, matrixWt.row(i-lagsModelMax), E, T, S,
                       nETS, nNonSeasonal, nSeasonal, nArima, nXreg, nComponents, vectorG, vecErrors(i-lagsModelMax));

                // Failsafe for cases, when nan values appear
                if(matrixVt.col(i).has_nan()){
                    matrixVt.col(i) = matrixVt(lagrows);
                }
                /* Failsafe for cases when unreasonable value for state vector was produced */
                // if(!matrixVt.col(i).is_finite()){
                //     matrixVt.col(i) = matrixVt(lagrows);
                // }
                if(T=='M'){
                    if((matrixVt(0,i) <= 0) | (matrixVt(1,i) <= 0)){
                        matrixVt(0,i) = arma::as_scalar(matrixVt(lagrows.row(0)));
                        matrixVt(1,i) = arma::as_scalar(matrixVt(lagrows.row(1)));
                    }
                }

                /* Renormalise components if the seasonal model is chosen */
                // if(S!='N'){
                //     if(double(i+1) / double(lagsModelMax) == double((i+1) / lagsModelMax)){
                //         matrixVt.cols(i-lagsModelMax+1,i) = normaliser(matrixVt.cols(i-lagsModelMax+1,i), obsall, lagsModelMax, S, T);
                //     }
                // }

                /* # Transition equation for xreg */
            }

            // Fill in the head of the series
            for (int i=lagsModelMax-1; i>=0; i=i-1) {
                lagrows = i * nComponents + lagsInternal - lagsModifier + nComponents - 1;
                matrixVt.col(i) = adamFvalue(matrixVt(lagrows), matrixF, E, T, S, nETS, nNonSeasonal, nSeasonal, nArima, nComponents);

                // /* Failsafe for cases when unreasonable value for state vector was produced */
                // if(!matrixVt.col(i).is_finite()){
                //     matrixVt.col(i) = matrixVt(lagrows);
                // }
                if((S=='M') && (matrixVt(nNonSeasonal,i) <= 0)){
                    matrixVt(nNonSeasonal,i) = arma::as_scalar(matrixVt(lagrows.row(nNonSeasonal)));
                }
                if(T=='M'){
                    if((matrixVt(0,i) <= 0) | (matrixVt(1,i) <= 0)){
                        matrixVt(0,i) = arma::as_scalar(matrixVt(lagrows.row(0)));
                        matrixVt(1,i) = arma::as_scalar(matrixVt(lagrows.row(1)));
                    }
                }
            }
        }
    }

    return List::create(Named("matVt") = matrixVt, Named("yFitted") = vecYfit,
                        Named("errors") = vecErrors);
}

/* # Wrapper for fitter */
// [[Rcpp::export]]
RcppExport SEXP adamFitterWrap(SEXP matVt, SEXP matWt, SEXP matF, SEXP vecG,
                               SEXP lagsModelAll, SEXP Etype, SEXP Ttype, SEXP Stype,
                               SEXP componentsNumberETS, SEXP componentsNumberETSSeasonal,
                               SEXP componentsNumberArima, SEXP xregNumber,
                               SEXP yInSample, SEXP ot, SEXP backcasting){

    NumericMatrix matvt_n(matVt);
    arma::mat matrixVt(matvt_n.begin(), matvt_n.nrow(), matvt_n.ncol());

    NumericMatrix matWt_n(matWt);
    arma::mat matrixWt(matWt_n.begin(), matWt_n.nrow(), matWt_n.ncol(), false);

    NumericMatrix matF_n(matF);
    arma::mat matrixF(matF_n.begin(), matF_n.nrow(), matF_n.ncol(), false);

    NumericMatrix vecg_n(vecG);
    arma::vec vectorG(vecg_n.begin(), vecg_n.nrow(), false);

    IntegerVector lagsModel_n(lagsModelAll);
    arma::uvec lags = as<arma::uvec>(lagsModel_n);

    char E = as<char>(Etype);
    char T = as<char>(Ttype);
    char S = as<char>(Stype);

    unsigned int nSeasonal = as<int>(componentsNumberETSSeasonal);
    unsigned int nNonSeasonal = as<int>(componentsNumberETS) - nSeasonal;
    unsigned int nArima = as<int>(componentsNumberArima);
    unsigned int nXreg = as<int>(xregNumber);

    NumericMatrix yt_n(yInSample);
    arma::vec vectorYt(yt_n.begin(), yt_n.nrow(), false);

    NumericVector ot_n(ot);
    arma::vec vectorOt(ot_n.begin(), ot_n.size(), false);

    bool backcast = as<bool>(backcasting);

    return wrap(adamFitter(matrixVt, matrixWt, matrixF, vectorG,
                           lags, E, T, S,
                           nNonSeasonal, nSeasonal, nArima, nXreg,
                           vectorYt, vectorOt, backcast));
}


/* # Function produces the point forecasts for the specified model */
arma::vec adamForecaster(arma::mat const &matrixVt, arma::mat const &matrixWt, arma::mat const &matrixF,
                         arma::uvec lags, char const &E, char const &T, char const &S,
                         unsigned int const &nNonSeasonal, unsigned int const &nSeasonal,
                         unsigned int const &nArima, unsigned int const &nXreg,
                         unsigned int const &horizon){
    unsigned int lagslength = lags.n_rows;
    unsigned int lagsModelMax = max(lags);
    unsigned int hh = horizon + lagsModelMax;
    unsigned int nETS = nNonSeasonal + nSeasonal;
    unsigned int nComponents = matrixVt.n_rows;
    arma::uvec lagrows(lagslength, arma::fill::zeros);

    arma::vec vecYfor(horizon, arma::fill::zeros);
    arma::mat matrixVtnew(nComponents, hh, arma::fill::zeros);

    lags = lags * nComponents;

    for(unsigned int i=0; i<lagslength; i=i+1){
        lags(i) = lags(i) + (lagslength - i - 1);
    }

    matrixVtnew.submat(0,0,nComponents-1,lagsModelMax-1) = matrixVt.submat(0,0,nComponents-1,lagsModelMax-1);

    /* # Fill in the new xt matrix using F. Do the forecasts. */
    for (unsigned int i=lagsModelMax; i<hh; i=i+1) {
        lagrows = i * nComponents - lags + nComponents - 1;
        matrixVtnew.col(i) = adamFvalue(matrixVtnew(lagrows), matrixF, E, T, S, nETS, nNonSeasonal, nSeasonal, nArima, nComponents);

        vecYfor.row(i-lagsModelMax) = adamWvalue(matrixVtnew(lagrows), matrixWt.row(i-lagsModelMax), E, T, S,
                    nETS, nNonSeasonal, nSeasonal, nArima, nXreg, nComponents);
    }

    // return List::create(Named("matVt") = matrixVtnew, Named("yForecast") = vecYfor);
    return vecYfor;
}

/* # Wrapper for forecaster */
// [[Rcpp::export]]
RcppExport SEXP adamForecasterWrap(SEXP matVt, SEXP matWt, SEXP matF,
                                   SEXP lagsModelAll, SEXP Etype, SEXP Ttype, SEXP Stype,
                                   SEXP componentsNumberETS, SEXP componentsNumberETSSeasonal,
                                   SEXP componentsNumberArima, SEXP xregNumber,
                                   SEXP h){

    NumericMatrix matvt_n(matVt);
    arma::mat matrixVt(matvt_n.begin(), matvt_n.nrow(), matvt_n.ncol(), false);

    NumericMatrix matWt_n(matWt);
    arma::mat matrixWt(matWt_n.begin(), matWt_n.nrow(), matWt_n.ncol(), false);

    NumericMatrix matF_n(matF);
    arma::mat matrixF(matF_n.begin(), matF_n.nrow(), matF_n.ncol(), false);

    IntegerVector lagsModel_n(lagsModelAll);
    arma::uvec lags = as<arma::uvec>(lagsModel_n);

    char E = as<char>(Etype);
    char T = as<char>(Ttype);
    char S = as<char>(Stype);

    unsigned int nSeasonal = as<int>(componentsNumberETSSeasonal);
    unsigned int nNonSeasonal = as<int>(componentsNumberETS) - nSeasonal;
    unsigned int nArima = as<int>(componentsNumberArima);
    unsigned int nXreg = as<int>(xregNumber);

    unsigned int horizon = as<int>(h);

    return wrap(adamForecaster(matrixVt, matrixWt, matrixF,
                               lags, E, T, S,
                               nNonSeasonal, nSeasonal,
                               nArima, nXreg,
                               horizon));
}

/* # Function produces matrix of errors based on multisteps forecast */
arma::mat adamErrorer(arma::mat const &matrixVt, arma::mat const &matrixWt, arma::mat const &matrixF,
                      arma::uvec &lags, char const &E, char const &T, char const &S,
                      unsigned int const &nNonSeasonal, unsigned int const &nSeasonal,
                      unsigned int const &nArima, unsigned int const &nXreg,
                      unsigned int const &horizon,
                      arma::vec const &vectorYt, arma::vec const &vectorOt){
    unsigned int obs = vectorYt.n_rows;
    // This is needed for cases, when hor>obs
    unsigned int hh = 0;
    arma::mat matErrors(horizon, obs, arma::fill::zeros);
    unsigned int lagsModelMax = max(lags);

    for(unsigned int i = 0; i < (obs-horizon); i=i+1){
        hh = std::min(horizon, obs-i);
        // matErrors.submat(0, i, hh-1, i) = (vectorOt.rows(i, i+hh-1) % errorvf(vectorYt.rows(i, i+hh-1),
        matErrors.submat(0, i, hh-1, i) = (errorvf(vectorYt.rows(i, i+hh-1),
                                           adamForecaster(matrixVt.cols(i,i+lagsModelMax-1), matrixWt.rows(i,i+hh-1),
                                                          matrixF, lags, E, T, S, nNonSeasonal, nSeasonal, nArima, nXreg, hh), E));
    }

    // Cut-off the redundant last part
    if(obs>horizon){
        matErrors = matErrors.cols(0,obs-horizon-1);
    }

    // Fix for GV in order to perform better in the sides of the series
    // for(int i=0; i<(hor-1); i=i+1){
    //     matErrors.submat((hor-2)-(i),i+1,(hor-2)-(i),hor-1) = matErrors.submat(hor-1,0,hor-1,hor-i-2) * sqrt(1.0+i);
    // }

    return matErrors.t();
}

/* # Wrapper for error function */
// [[Rcpp::export]]
RcppExport SEXP adamErrorerWrap(SEXP matVt, SEXP matWt, SEXP matF,
                                SEXP lagsModelAll, SEXP Etype, SEXP Ttype, SEXP Stype,
                                SEXP componentsNumberETS, SEXP componentsNumberETSSeasonal,
                                SEXP componentsNumberArima, SEXP xregNumber,
                                SEXP h, SEXP yInSample, SEXP ot){

    NumericMatrix matvt_n(matVt);
    arma::mat matrixVt(matvt_n.begin(), matvt_n.nrow(), matvt_n.ncol(), false);

    NumericMatrix matWt_n(matWt);
    arma::mat matrixWt(matWt_n.begin(), matWt_n.nrow(), matWt_n.ncol(), false);

    NumericMatrix matF_n(matF);
    arma::mat matrixF(matF_n.begin(), matF_n.nrow(), matF_n.ncol(), false);

    IntegerVector lagsModel_n(lagsModelAll);
    arma::uvec lags = as<arma::uvec>(lagsModel_n);

    char E = as<char>(Etype);
    char T = as<char>(Ttype);
    char S = as<char>(Stype);

    unsigned int nSeasonal = as<int>(componentsNumberETSSeasonal);
    unsigned int nNonSeasonal = as<int>(componentsNumberETS) - nSeasonal;
    unsigned int nArima = as<int>(componentsNumberArima);
    unsigned int nXreg = as<int>(xregNumber);

    unsigned int horizon = as<int>(h);

    NumericMatrix yt_n(yInSample);
    arma::vec vectorYt(yt_n.begin(), yt_n.nrow(), false);

    NumericVector ot_n(ot);
    arma::vec vectorOt(ot_n.begin(), ot_n.size(), false);

    return wrap(adamErrorer(matrixVt, matrixWt, matrixF,
                            lags, E, T, S,
                            nNonSeasonal, nSeasonal, nArima, nXreg,
                            horizon, vectorYt, vectorOt));
}