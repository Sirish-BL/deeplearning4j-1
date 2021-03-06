/*******************************************************************************
 * Copyright (c) 2015-2018 Skymind, Inc.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License, Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

//
// Created by raver119 on 16.10.2017.
//

#include <ops/declarable/LegacyReduceBoolOp.h>
#include <helpers/TAD.h>
#include <helpers/ShapeUtils.h>

namespace nd4j {
    namespace ops {
        LegacyReduceBoolOp::LegacyReduceBoolOp() : LegacyOp::LegacyOp(1) {
            //
        }

        LegacyReduceBoolOp::LegacyReduceBoolOp(int opNum) : LegacyOp::LegacyOp(1, opNum) {
            //this->_opNum = opNum;
        }

        LegacyOp* LegacyReduceBoolOp::clone() {
            return new LegacyReduceBoolOp(this->_opNum);
        }

        Nd4jStatus LegacyReduceBoolOp::validateAndExecute(Context &block) {
            auto x = INPUT_VARIABLE(0);


            int opNum = block.opNum() < 0 ? this->_opNum : block.opNum();
            nd4j_debug("Executing LegacyReduceFloatOp: [%i]\n", opNum);

            bool allAxes = false;

            if (block.width() == 1) {
                auto z = OUTPUT_VARIABLE(0);

                if (block.getIArguments()->size() == x->rankOf())
                    allAxes = true;

                if ((block.getIArguments()->size() == 0) ||
                    (block.getIArguments()->size() == 1 && INT_ARG(0) == MAX_INT) || allAxes) {
                    // scalar
                    NativeOpExcutioner::execReduceBoolScalar(opNum, x->getBuffer(), x->getShapeInfo(), block.getTArguments()->data(), z->buffer(), z->shapeInfo());
                } else {
                    // TAD
                    std::vector<int> dims(*block.getIArguments());

                    for (int e = 0; e < dims.size(); e++)
                        if (dims[e] < 0)
                            dims[e] += x->rankOf();

                    std::sort(dims.begin(), dims.end());

                    REQUIRE_TRUE(dims.size() > 0, 0, "Some dimensions required for reduction!");

                    shape::TAD tad(x->getShapeInfo(), dims.data(), dims.size());
                    tad.createTadOnlyShapeInfo();
                    tad.createOffsets();

                    NativeOpExcutioner::execReduceBool(opNum, x->getBuffer(), x->getShapeInfo(), block.getTArguments()->data(), z->getBuffer(), z->getShapeInfo(), dims.data(), (int) dims.size(), tad.tadOnlyShapeInfo, tad.tadOffsets);
                }

                STORE_RESULT(*z);
            } else {
                auto indices = INPUT_VARIABLE(1);
                if (indices->lengthOf() == x->rankOf())
                    allAxes = true;

                //indices->printIndexedBuffer("indices");

                std::vector<int> axis(indices->lengthOf());
                for (int e = 0; e < indices->lengthOf(); e++) {
                    // lol otherwise we segfault on macOS
                    int f = indices->e<int>(e);
                    axis[e] = f >= 0 ? f : f += x->rankOf();
                }

                if ((block.getIArguments()->size() == 1 && INT_ARG(0) == MAX_INT) || allAxes) {
                    auto z = OUTPUT_VARIABLE(0);

                    auto b = x->getBuffer();
                    auto s = x->shapeInfo();
                    auto e = block.numT() > 0 ? block.getTArguments()->data() : nullptr;

                    //x->printIndexedBuffer("x");

                    // scalar
                    NativeOpExcutioner::execReduceBoolScalar(opNum, b, s, e, z->buffer(), z->shapeInfo());
                } else {
                    // TAD
                    if (indices->lengthOf() > 1)
                        std::sort(axis.begin(), axis.end());

                    REQUIRE_TRUE(axis.size() > 0, 0, "Some dimensions required for reduction!");

                    shape::TAD tad(x->getShapeInfo(), axis.data(), axis.size());
                    tad.createTadOnlyShapeInfo();
                    tad.createOffsets();

                    auto newShape = ShapeUtils::evalReduceShapeInfo(x->ordering(), axis, *x);
                    auto z = new NDArray(newShape, x->getWorkspace());

                    NativeOpExcutioner::execReduceBool(opNum, x->getBuffer(), x->getShapeInfo(), block.getTArguments()->data(), z->getBuffer(), z->getShapeInfo(), axis.data(), (int) axis.size(), tad.tadOnlyShapeInfo, tad.tadOffsets);

                    RELEASE(newShape, x->getWorkspace());


                    // keepDims processing, for TF compatibility
                    if (block.getIArguments()->size() > 0 && block.getIArguments()->at(0) == 1) {
                        // z->printShapeInfo("z shape before");
                        std::vector<Nd4jLong> newshape(z->getShapeAsVector());
                        for (int e = 0; e < axis.size(); e++) {
                            auto a = axis.at(e);
                            newshape.insert(newshape.begin() + a, 1);
                        }
                        z->reshapei(z->ordering(), newshape);
                        // z->printShapeInfo("z shape after");
                    }

                    OVERWRITE_RESULT(z);
                }
            }

            return ND4J_STATUS_OK;
        }

        /**
        *   For all reductions rules are simple: either you return scalar, or you return reduced NDArray.
        *   It solely depends on input shape, and requested dimensions
        */
        ShapeList *LegacyReduceBoolOp::calculateOutputShape(ShapeList *inputShape, nd4j::graph::Context &block) {
            auto inShape = inputShape->at(0);

            Nd4jLong *newShape;

            bool allAxes = false;

            auto keepDims = block.numB() > 0 ? B_ARG(0) : false;
            auto newFormat = block.numB() > 1 ? B_ARG(1) : true;

            auto axis = block.width() > 1 ? INPUT_VARIABLE(1)->asVectorT<int>() : *block.getAxis();

            if (axis.size() == shape::rank(inShape))
                allAxes = true;

            // in this case we're building proper shape for reduction
            auto array = new NDArray(nullptr, inShape, block.getWorkspace());
            array->triggerAllocationFlag(false, false);
            newShape = ShapeUtils::evalReduceShapeInfo(shape::order(inShape), axis, *array, keepDims, !newFormat, block.workspace());
            ArrayOptions::setDataType(newShape, DataType::BOOL);
            delete array;

            return SHAPELIST(newShape);
        }
    }
}