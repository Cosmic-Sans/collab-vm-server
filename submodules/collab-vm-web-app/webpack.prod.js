const webpack = require("webpack");
const merge = require("webpack-merge");
const common = require("./webpack.common.js");
const MiniCssExtractPlugin = require("mini-css-extract-plugin");
const TerserPlugin = require("terser-webpack-plugin");

common.setHtmlWebpackPluginOptions({
  minify: {removeComments: true, collapseWhitespace: true}
});

module.exports = merge(common.config, {
  mode: "production",
  module: {
      rules: [
        {
          test: /\.css$/i,
          use: ['style-loader', 'css-loader'],
        }
      ]
    },
    plugins: [
       new webpack.DefinePlugin({
         __DEV__: false
       }),
       new MiniCssExtractPlugin()
    ],
  optimization: {
    /*
    // Why doesn't this work?
     splitChunks: {
       chunks: 'all'
     },
     */
    minimizer: [new TerserPlugin()]
   }
});

