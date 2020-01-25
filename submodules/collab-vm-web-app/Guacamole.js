const requireAll = ctx => Object.assign.apply(null, ctx.keys().map(ctx));
module.exports = requireAll(require.context("GuacModulesPath", false, /\.js$/));

