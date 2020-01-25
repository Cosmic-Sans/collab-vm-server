const merge = require("webpack-merge");
const common = require("./webpack.common.js");
const path = require("path");

const webpack = require("webpack");
common.setHtmlWebpackPluginOptions({minify: false});

module.exports = merge(common.config, {
  mode: "development",
  devtool: "inline-source-map",
  module: {
    rules: [
      {
        test: require.resolve('jquery'),
        use: [
          {
          loader: "expose-loader",
          options: "$"
          }
        ]
      },
      {
        test: path.resolve(__dirname, "admin.js"),
        use: [
          {
          loader: "expose-loader",
          options: "admin"
          }
        ]
      },
      {
        test: /\.css$/,
        use: ["style-loader", "css-loader"]
      },
		]
	},
	plugins: [
		new webpack.DefinePlugin({
			__DEV__: true
		})
	]
});

