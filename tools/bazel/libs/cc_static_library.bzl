def _cc_static_library_impl(ctx):
    cc = ctx.attr.dep[CcInfo]
    libraries = []
    for link_input in cc.linking_context.linker_inputs.to_list():
        for library in link_input.libraries:
            libraries += library.objects
    args = ["r", ctx.outputs.out.path] + [f.path for f in libraries]
    ctx.actions.run(
        inputs = libraries,
        outputs = [ctx.outputs.out],
        executable = "/usr/bin/ar",
        arguments = args,
    )
    return [DefaultInfo()]

cc_static_library = rule(
    implementation = _cc_static_library_impl,
    attrs = {
        "dep": attr.label(providers = [CcInfo]),
        "output": attr.string(),
    },
    outputs = {"out": "%{output}"},
)