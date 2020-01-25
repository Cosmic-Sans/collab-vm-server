const recaptchaEnabled = true;
const recaptchaSiteKey = "6LeIxAcTAAAAAJcZVRqyHh71UMIEGNQ_MXjiZKhI";
const publicPath = "/";
const collabVmPath = "build/collab-vm-web-app/";
// Gets the address of the WebSocket server
const getWebSocketAddress = function() {
  // Use the URL of the current page
  return (window.location.protocol === "https:" ? "wss://" : "ws://") + window.location.host;
};

const path = require("path");
const webpack = require("webpack");
const mainHtmlOptions = {
  inject: "body",
  template: "index.html",
  filename: "index.html",
  chunks: ["main"]
};
const adminHtmlOptions = {
  inject: "body",
  template: "admin.html",
  filename: "admin.html",
  chunks: ["admin"]
};
const htmlPlugin = require("html-webpack-plugin");
const mainHtmlWebpackPlugin  = new htmlPlugin(mainHtmlOptions);
const adminHtmlWebpackPlugin = new htmlPlugin(adminHtmlOptions);
const guacModulesPath = path.resolve(__dirname, "submodules/guacamole-client/guacamole-common-js/src/main/webapp/modules");
const semanticUiPath = path.resolve(__dirname, "node_modules/fomantic-ui/dist/semantic.js");

module.exports.config = {
  entry: {
    main: "main.js",
    admin: "admin.js"
  },
  output: {
    filename: "[name].js",
    path: __dirname + "/dist",
    publicPath: publicPath
    },
  module: {
  	rules: [
    {
      test: /collab-vm-web-app\.js$/,
      use: [
        "source-map-loader",
        "imports-loader?Module=runtime.js",
        "exports-loader?Module"
      ]
    },
		{test:guacModulesPath,
			use:["imports-loader?Guacamole", "exports-loader?Guacamole"]},
    {test:semanticUiPath, use:["imports-loader?jQuery=jquery"]},
      {
      	test: /\.(eot|png|svg|ttf|woff|woff2)$/,
      	use: "file-loader"
      },
      {
      	test: /\.wasm|\.wast$/,
        // Suppresses the use of webpack's default wasm loader
        type: "javascript/auto",
      	use: "file-loader"
      }
      
    ],
  },
	resolve: {
		 modules: [
       "node_modules",
       "."
			     ],
		alias: {
      "collab-vm-web-app$": path.resolve(collabVmPath, "collab-vm-web-app.js"),
      "collab-vm-web-app\.wasm$": path.resolve(collabVmPath, "collab-vm-web-app.wasm"),
      "collab-vm-web-app\.wasm\.map$": path.resolve(collabVmPath, "collab-vm-web-app.wasm.map"),
      "collab-vm-web-app\.wast$": path.resolve(collabVmPath, "collab-vm-web-app.wast"),
			"Guacamole$": path.resolve(__dirname, "Guacamole.js"),
			"GuacModulesPath$": guacModulesPath,
      "semantic-ui$": semanticUiPath,
      "semantic-ui.css$": path.resolve(__dirname, "node_modules/fomantic-ui/dist/semantic.css"),
      "tabulator_semantic-ui.css$": path.resolve(__dirname, "node_modules/tabulator-tables/dist/css/semantic-ui/tabulator_semantic-ui.css")
		}
	},
	plugins: [
		new webpack.DefinePlugin({
			WEBSOCKET_ADDRESS: "(" + getWebSocketAddress.toString() + ")()",
      RECAPTCHA_ENABLED: recaptchaEnabled,
      RECAPTCHA_SITE_KEY: '"' + recaptchaSiteKey + '"'
		}),
    mainHtmlWebpackPlugin,
    adminHtmlWebpackPlugin
	],
  // Return an empty object to require("fs") in collab-vm-web-app.js
  node: {
       fs: "empty"
  },
	devServer: {
      historyApiFallback: {
      rewrites: [
        { from: new RegExp(`^${publicPath}admin/?$`), to: publicPath + "admin.html" },
        { from: /./, to: publicPath + "index.html" }
      ]
    }
	}
};
module.exports.setHtmlWebpackPluginOptions = options =>
     Object.assign(mainHtmlWebpackPlugin.options, options)
  && Object.assign(adminHtmlWebpackPlugin.options, options);
