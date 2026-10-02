#!/usr/bin/env python3
"""Average the weights of fine-tuned BeatNet models: OUT.onnx IN1.onnx IN2.onnx ...

Every model fine-tuned from the same GTZAN start has the same graph, so the mean
of each initializer is the mean of the weights ("model soup"). One fine-tune's
octave readings swing with its seed (62 -> 80% at the right level, docs/TODO.md
item 87); the mean model is one network, same cost in the app.
"""
import sys
import numpy as np
import onnx
from onnx import numpy_helper

out, ins = sys.argv[1], sys.argv[2:]
models = [onnx.load(p) for p in ins]
base = models[0]
for i, init in enumerate(base.graph.initializer):
    arrs = [numpy_helper.to_array(m.graph.initializer[i]) for m in models]
    assert all(m.graph.initializer[i].name == init.name for m in models), init.name
    if np.issubdtype(arrs[0].dtype, np.floating):
        init.CopyFrom(numpy_helper.from_array(np.mean(arrs, 0).astype(arrs[0].dtype), init.name))
onnx.save(base, out)
print('wrote', out, 'from', len(ins), 'models')
