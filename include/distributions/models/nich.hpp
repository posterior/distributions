// Copyright (c) 2014, Salesforce.com, Inc.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// - Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// - Neither the name of Salesforce.com nor the names of its contributors
//   may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
// TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <distributions/common.hpp>
#include <distributions/special.hpp>
#include <distributions/random.hpp>
#include <distributions/vector.hpp>

namespace distributions {
namespace normal_inverse_chi_sq {
typedef float Value;
struct Group;
struct Scorer;
struct Sampler;
struct Mixture;

struct Model
{
float mu;
float kappa;
float sigmasq;
float nu;

Model plus_group(const Group & group) const;

static Model EXAMPLE ();
};

inline Model Model::EXAMPLE ()
{
    Model model;
    model.mu = 0.0;
    model.kappa = 1.0;
    model.sigmasq = 1.0;
    model.nu = 1.0;
    return model;
}

struct Group
{
    uint32_t count;
    float mean;
    float count_times_variance;

    void init (
            const Model &,
            rng_t &)
    {
        count = 0;
        mean = 0.f;
        count_times_variance = 0.f;
    }

    void add_value (
            const Model &,
            const Value & value,
            rng_t &)
    {
        ++count;
        float delta = value - mean;
        mean += delta / count;
        count_times_variance += delta * (value - mean);
    }

    void remove_value (
            const Model &,
            const Value & value,
            rng_t &)
    {
        float total = mean * count;
        float delta = value - mean;
        DIST_ASSERT(count > 0, "Can't remove empty group");

        --count;
        if (count == 0) {
            mean = 0.f;
        } else {
            mean = (total - value) / count;
        }
        if (count <= 1) {
            count_times_variance = 0.f;
        } else {
            count_times_variance -= delta * (value - mean);
        }
    }

    void merge (
            const Model &,
            const Group & source,
            rng_t &)
    {
        uint32_t total_count = count + source.count;
        float delta = source.mean - mean;
        float source_part = float(source.count) / total_count;
        float cross_part = count * source_part;
        count = total_count;
        mean += source_part * delta;
        count_times_variance +=
            source.count_times_variance + cross_part * sqr(delta);
    }
};

struct Sampler
{
    float mu;
    float sigmasq;

    void init (
            const Model & model,
            const Group & group,
            rng_t & rng)
    {
        Model post = model.plus_group(group);
        sigmasq = post.nu * post.sigmasq / sample_chisq(rng, post.nu);
        mu = sample_normal(rng, post.mu, sigmasq / post.kappa);
    }

    Value eval (
            const Model & model,
            rng_t & rng) const
    {
        return sample_normal(rng, mu, sigmasq);
    }
};

struct Scorer
{
    float score;
    float log_coeff;
    float precision;
    float mean;

    void init (
            const Model & model,
            const Group & group,
            rng_t &)
    {
        Model post = model.plus_group(group);
        float lambda = post.kappa / ((post.kappa + 1.f) * post.sigmasq);
        score =
            fast_lgamma_nu(post.nu) + 0.5f * fast_log(lambda / (M_PIf * post.nu));
        log_coeff = -0.5f * post.nu - 0.5f;
        precision = lambda / post.nu;
        mean = post.mu;
    }

    float eval (
            const Model & model,
            const Value & value,
            rng_t &) const
    {
        return score
             + log_coeff * fast_log(
                 1.f + precision * sqr(value - mean));
    }
};

struct Mixture
{
    typedef normal_inverse_chi_sq::Value Value;
    typedef normal_inverse_chi_sq::Model Model;
    typedef normal_inverse_chi_sq::Group Group;
    typedef normal_inverse_chi_sq::Scorer Scorer;

    std::vector<Group> groups;
    VectorFloat score;
    VectorFloat log_coeff;
    VectorFloat precision;
    VectorFloat mean;
    mutable VectorFloat temp;

    private:

    void _update_group (
            const Model & model,
            size_t groupid,
            rng_t & rng)
    {
        const Group & group = groups[groupid];
        Scorer scorer;
        scorer.init(model, group, rng);
        score[groupid] = scorer.score;
        log_coeff[groupid] = scorer.log_coeff;
        precision[groupid] = scorer.precision;
        mean[groupid] = scorer.mean;
    }

    void _resize (
            const Model & model,
            size_t group_count)
    {
        groups.resize(group_count);
        score.resize(group_count);
        log_coeff.resize(group_count);
        precision.resize(group_count);
        mean.resize(group_count);
        temp.resize(group_count);
    }

    public:

    void init (
            const Model & model,
            rng_t & rng)
    {
        const size_t group_count = groups.size();
        _resize(model, group_count);
        for (size_t groupid = 0; groupid < group_count; ++groupid) {
            _update_group(model, groupid, rng);
        }
    }

    void add_group (
            const Model & model,
            rng_t & rng)
    {
        const size_t groupid = groups.size();
        const size_t group_count = groupid + 1;
        _resize(model, group_count);
        groups.back().init(model, rng);
        _update_group(model, groupid, rng);
    }

    void remove_group (
            const Model & model,
            size_t groupid)
    {
        DIST_ASSERT1(groupid < groups.size(), "bad groupid: " << groupid);
        const size_t group_count = groups.size() - 1;
        if (groupid != group_count) {
            std::swap(groups[groupid], groups.back());
            score[groupid] = score.back();
            log_coeff[groupid] = log_coeff.back();
            precision[groupid] = precision.back();
            mean[groupid] = mean.back();
        }
        _resize(model, group_count);
    }

    void add_value (
            const Model & model,
            size_t groupid,
            const Value & value,
            rng_t & rng)
    {
        DIST_ASSERT1(groupid < groups.size(), "bad groupid: " << groupid);
        Group & group = groups[groupid];
        group.add_value(model, value, rng);
        _update_group(model, groupid, rng);
    }

    void remove_value (
            const Model & model,
            size_t groupid,
            const Value & value,
            rng_t & rng)
    {
        DIST_ASSERT2(groupid < groups.size(), "bad groupid: " << groupid);
        Group & group = groups[groupid];
        group.remove_value(model, value, rng);
        _update_group(model, groupid, rng);
    }

    void score_value (
            const Model & model,
            const Value & value,
            AlignedFloats scores_accum,
            rng_t & rng) const
    {
        if (DIST_DEBUG_LEVEL >= 2) {
            DIST_ASSERT_EQ(scores_accum.size(), groups.size());
        }
        _score_value(model, value, scores_accum, rng);
    }

    private:

    void _score_value (
            const Model & model,
            const Value & value,
            AlignedFloats scores_accum,
            rng_t &) const;
};

inline Model Model::plus_group (const Group & group) const
{
    Model post;
    float mu_1 = mu - group.mean;
    post.kappa = kappa + group.count;
    post.mu = (kappa * mu + group.mean * group.count) / post.kappa;
    post.nu = nu + group.count;
    post.sigmasq = 1.f / post.nu * (
        nu * sigmasq
        + group.count_times_variance
        + (group.count * kappa * mu_1 * mu_1) / post.kappa);
    return post;
}

} // namespace normal_inverse_chi_sq

inline normal_inverse_chi_sq::Value sample_value (
        const normal_inverse_chi_sq::Model & model,
        const normal_inverse_chi_sq::Group & group,
        rng_t & rng)
{
    normal_inverse_chi_sq::Sampler sampler;
    sampler.init(model, group, rng);
    return sampler.eval(model, rng);
}

inline float score_value (
        const normal_inverse_chi_sq::Model & model,
        const normal_inverse_chi_sq::Group & group,
        const normal_inverse_chi_sq::Value & value,
        rng_t & rng)
{
    normal_inverse_chi_sq::Scorer scorer;
    scorer.init(model, group, rng);
    return scorer.eval(model, value, rng);
}

inline float score_group (
        const normal_inverse_chi_sq::Model & model,
        const normal_inverse_chi_sq::Group & group,
        rng_t &)
{
    normal_inverse_chi_sq::Model post = model.plus_group(group);
    float log_pi = 1.1447298858493991f;
    float score = fast_lgamma(0.5f * post.nu) - fast_lgamma(0.5f * model.nu);
    score += 0.5f * fast_log(model.kappa / post.kappa);
    score += 0.5f * model.nu * (fast_log(model.nu * model.sigmasq))
           - 0.5f * post.nu * fast_log(post.nu * post.sigmasq);
    score += -0.5f * group.count * log_pi;
    return score;
}

} // namespace distributions
