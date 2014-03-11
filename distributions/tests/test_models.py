import math
import random
import numpy
from nose.tools import (
    assert_true,
    assert_in,
    assert_is_instance,
    assert_not_equal,
    assert_greater,
)
from distributions.util import (
    density_goodness_of_fit,
    discrete_goodness_of_fit,
)
from distributions.tests.util import (
    assert_hasattr,
    assert_close,
    assert_all_close,
    list_models,
    import_model,
    seed_all,
)

DATA_COUNT = 20
SAMPLE_COUNT = 2000
MIN_GOODNESS_OF_FIT = 1e-3

MODULES = {
    '{flavor}.models.{name}'.format(**spec): import_model(spec)
    for spec in list_models()
}


def iter_examples(Model):
    assert_hasattr(Model, 'EXAMPLES')
    EXAMPLES = Model.EXAMPLES
    assert_is_instance(EXAMPLES, list)
    assert_true(EXAMPLES, 'no examples provided')
    for i, EXAMPLE in enumerate(EXAMPLES):
        print 'example {}/{}'.format(1 + i, len(Model.EXAMPLES))
        assert_in('model', EXAMPLE)
        assert_in('values', EXAMPLE)
        values = EXAMPLE['values']
        assert_is_instance(values, list)
        count = len(values)
        assert_true(
            count >= 7,
            'Add more example values (expected >= 7, found {})'.format(count))
        yield EXAMPLE


def _test_interface(name):
    module = MODULES[name]
    assert_hasattr(module, 'Model')
    Model = module.Model

    for typename in ['Value', 'Group']:
        assert_hasattr(Model, typename)
        assert_is_instance(getattr(Model, typename), type)

    for EXAMPLE in iter_examples(Model):
        model = Model.model_load(EXAMPLE['model'])
        values = EXAMPLE['values']
        for value in values:
            assert_is_instance(value, Model.Value)

        group1 = model.Group()
        model.group_init(group1)
        for value in values:
            model.group_add_value(group1, value)
        group2 = model.group_create(values)
        assert_close(group1.dump(), group2.dump())

        for value in values:
            model.group_remove_value(group2, value)
        assert_not_equal(group1, group2)
        model.group_merge(group2, group1)

        for value in values:
            model.score_value(group1, value)
        for _ in xrange(10):
            value = model.sample_value(group1)
            model.score_value(group1, value)
            model.sample_group(10)
        model.score_group(group1)
        model.score_group(group2)

        assert_close(model.dump(), EXAMPLE['model'])
        assert_close(model.dump(), Model.model_dump(model))
        assert_close(group1.dump(), Model.group_dump(group1))


def _test_add_remove(name):
    '''
    Test group_add_value, group_remove_value, score_group, score_value
    '''
    Model = MODULES[name].Model
    for EXAMPLE in iter_examples(Model):

        model = Model.model_load(EXAMPLE['model'])
        #model.realize()
        #values = model['values'][:]

        values = []
        group = model.group_create()
        score = 0.0
        assert_close(model.score_group(group), score, err_msg='p(empty) != 1')

        for _ in range(DATA_COUNT):
            value = model.sample_value(group)
            values.append(value)
            score += model.score_value(group, value)
            model.group_add_value(group, value)

        group_all = model.group_load(model.group_dump(group))
        assert_close(
            score,
            model.score_group(group),
            err_msg='p(x1,...,xn) != p(x1) p(x2|x1) p(xn|...)')

        random.shuffle(values)

        for value in values:
            model.group_remove_value(group, value)

        group_empty = model.group_create()
        assert_close(
            group.dump(),
            group_empty.dump(),
            err_msg='group + values - values != group')

        random.shuffle(values)
        for value in values:
            model.group_add_value(group, value)
        assert_close(
            group.dump(),
            group_all.dump(),
            err_msg='group - values + values != group')


def _test_add_merge(name):
    '''
    Test group_add_value, group_merge
    '''
    Model = MODULES[name].Model
    for EXAMPLE in iter_examples(Model):
        model = Model.model_load(EXAMPLE['model'])
        values = EXAMPLE['values'][:]
        random.shuffle(values)
        group = model.group_create(values)

        for i in xrange(len(values) + 1):
            random.shuffle(values)
            group1 = model.group_create(values[:i])
            group2 = model.group_create(values[i:])
            model.group_merge(group1, group2)
            assert_close(group.dump(), group1.dump())


def _test_sample_seed(name):
    Model = MODULES[name].Model
    for EXAMPLE in iter_examples(Model):
        model = Model.model_load(EXAMPLE['model'])

        seed_all(0)
        group1 = model.group_create()
        values1 = [model.sample_value(group1) for _ in xrange(DATA_COUNT)]

        seed_all(0)
        group2 = model.group_create()
        values2 = [model.sample_value(group2) for _ in xrange(DATA_COUNT)]

        assert_close(values1, values2, 'values')


def _test_sample_value(name):
    seed_all(1)
    Model = MODULES[name].Model
    for EXAMPLE in iter_examples(Model):
        model = Model.model_load(EXAMPLE['model'])
        for values in [[], EXAMPLE['values']]:
            group = model.group_create(values)
            samples = [model.sample_value(group) for _ in xrange(SAMPLE_COUNT)]
            if Model.Value == int:
                probs_dict = {
                    value: math.exp(model.score_value(group, value))
                    for value in set(samples)
                }
                gof = discrete_goodness_of_fit(samples, probs_dict, plot=True)
            elif Model.Value == float:
                probs = numpy.exp([
                    model.score_value(group, value)
                    for value in samples
                ])
                gof = density_goodness_of_fit(samples, probs, plot=True)
            else:
                raise NotImplementedError(
                    'sampler test not implemented for {}'.format(Model.Value))
            print '{} gof = {:0.3g}'.format(name, gof)
            assert_greater(gof, MIN_GOODNESS_OF_FIT)


def _test_scorer(name):
    Model = MODULES[name].Model
    for EXAMPLE in iter_examples(Model):
        model = Model.model_load(EXAMPLE['model'])
        values = EXAMPLE['values']

        group = model.group_create()
        scorer1 = model.scorer_create()
        scorer2 = model.scorer_create(group)
        for value in values:
            score1 = model.scorer_eval(scorer1, value)
            score2 = model.scorer_eval(scorer2, value)
            score3 = model.score_value(group, value)
            assert_all_close([score1, score2, score3])


def _test_vector_scorer(name):
    Model = MODULES[name].Model
    for EXAMPLE in iter_examples(Model):
        model = Model.model_load(EXAMPLE['model'])
        values = EXAMPLE['values']

        groups = [
            model.group_create(values[i:j])
            for i in xrange(len(values))
            for j in xrange(i, 1 + len(values))
        ]
        group_count = len(groups)

        scorer = model.VectorScorer()
        model.vector_scorer_init(scorer, group_count)
        for index, group in enumerate(groups):
            model.vector_scorer_update(scorer, index, group)

        for value in values:
            scores1 = model.vector_scorer_eval(scorer, value)
            scores2 = [model.score_value(group, value) for group in groups]
            assert_close(scores1, scores2)


def test_module():
    for name in MODULES:
        Model = MODULES[name].Model
        yield _test_interface, name
        yield _test_add_remove, name
        yield _test_add_merge, name
        yield _test_sample_seed, name
        if hasattr(Model, 'scorer_create'):
            yield _test_scorer, name
        yield _test_sample_value, name
        #if hasattr(Model, 'VectorScorer'):
        #    yield _test_vector_scorer, name  # FIXME
