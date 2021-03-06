/*
 -----------------------------------------------------------------------------
 This source file is part of OGRE
 (Object-oriented Graphics Rendering Engine)
 For the latest info, see http://www.ogre3d.org/
 
 Copyright (c) 2000-2014 Torus Knot Software Ltd
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 -----------------------------------------------------------------------------
 */

#include "OgreGLSLESProgramPipeline.h"
#include "OgreStringConverter.h"
#include "OgreGLSLESProgram.h"
#include "OgreGLSLESProgramManager.h"
#include "OgreGpuProgramManager.h"
#include "OgreGLES2RenderSystem.h"
#include "OgreGLUniformCache.h"
#include "OgreGLES2HardwareUniformBuffer.h"
#include "OgreHardwareBufferManager.h"
#include "OgreGLUtil.h"
#include "OgreRoot.h"
#include "OgreGLES2Support.h"

namespace Ogre
{
    GLSLESProgramPipeline::GLSLESProgramPipeline(GLSLESProgram* vertexProgram, GLSLESProgram* fragmentProgram) :
    GLSLESProgramCommon(vertexProgram, fragmentProgram) { }

    GLSLESProgramPipeline::~GLSLESProgramPipeline()
    {
        OGRE_CHECK_GL_ERROR(glDeleteProgramPipelinesEXT(1, &mGLProgramHandle));
    }

    void GLSLESProgramPipeline::compileAndLink()
    {
        GLint linkStatus = 0;
        
        OGRE_CHECK_GL_ERROR(glGenProgramPipelinesEXT(1, &mGLProgramHandle));
        //OGRE_CHECK_GL_ERROR(glBindProgramPipelineEXT(mGLProgramHandle));

        // Compile and attach Vertex Program
        if(getVertexProgram())
        {
            if(getVertexProgram()->isLinked())
            {
                mLinked |= VERTEX_PROGRAM_LINKED;
            }
            else if(getMicrocodeFromCache(
                    getVertexProgram()->getName(),
                    getVertexProgram()->createGLProgramHandle()))
            {
                getVertexProgram()->setLinked(true);
                mLinked |= VERTEX_PROGRAM_LINKED;
                mTriedToLinkAndFailed = false;
            }
            else
            {
                if(!getVertexProgram()->compile(true)) {
                    LogManager::getSingleton().stream(LML_CRITICAL)
                            << "Vertex Program " << getVertexProgram()->getName()
                            << " failed to compile. See compile log above for details.";
                    mTriedToLinkAndFailed = true;
                    return;
                }
                GLuint programHandle = getVertexProgram()->getGLProgramHandle();

                bindFixedAttributes( programHandle );

                OGRE_CHECK_GL_ERROR(glProgramParameteriEXT(programHandle, GL_PROGRAM_SEPARABLE_EXT, GL_TRUE));
                getVertexProgram()->attachToProgramObject(programHandle);
                OGRE_CHECK_GL_ERROR(glLinkProgram(programHandle));
                OGRE_CHECK_GL_ERROR(glGetProgramiv(programHandle, GL_LINK_STATUS, &linkStatus));
                
                if(linkStatus)
                {
                    getVertexProgram()->setLinked(linkStatus);
                    mLinked |= VERTEX_PROGRAM_LINKED;
                }
                
                mTriedToLinkAndFailed = !linkStatus;
                
                GLSLES::logObjectInfo( getCombinedName() + String("GLSL vertex program result : "), programHandle );

                setSkeletalAnimationIncluded(getVertexProgram()->isSkeletalAnimationIncluded());
            }
        }
        
        // Compile and attach Fragment Program
        if(mFragmentProgram)
        {
            if(mFragmentProgram->isLinked())
            {
                mLinked |= FRAGMENT_PROGRAM_LINKED;
            }
            else if(getMicrocodeFromCache(
                    mFragmentProgram->getName(),
                    mFragmentProgram->createGLProgramHandle()))
            {
                mFragmentProgram->setLinked(true);
                mLinked |= FRAGMENT_PROGRAM_LINKED;
                mTriedToLinkAndFailed = false;
            }
            else
            {
                if(!mFragmentProgram->compile(true)) {
                    LogManager::getSingleton().stream(LML_CRITICAL)
                            << "Fragment Program " << mFragmentProgram->getName()
                            << " failed to compile. See compile log above for details.";
                    mTriedToLinkAndFailed = true;
                    return;
                }

                GLuint programHandle = mFragmentProgram->getGLProgramHandle();
                OGRE_CHECK_GL_ERROR(glProgramParameteriEXT(programHandle, GL_PROGRAM_SEPARABLE_EXT, GL_TRUE));
                mFragmentProgram->attachToProgramObject(programHandle);
                OGRE_CHECK_GL_ERROR(glLinkProgram(programHandle));
                OGRE_CHECK_GL_ERROR(glGetProgramiv(programHandle, GL_LINK_STATUS, &linkStatus));

                if(linkStatus)
                {
                    mFragmentProgram->setLinked(linkStatus);
                    mLinked |= FRAGMENT_PROGRAM_LINKED;
                }

                mTriedToLinkAndFailed = !linkStatus;

                GLSLES::logObjectInfo( getCombinedName() + String("GLSL fragment program result : "), programHandle );
            }
        }
        
        if(mLinked)
        {
            if(getVertexProgram() && getVertexProgram()->isLinked())
            {
                OGRE_CHECK_GL_ERROR(glUseProgramStagesEXT(mGLProgramHandle, GL_VERTEX_SHADER_BIT_EXT, getVertexProgram()->getGLProgramHandle()));
                _writeToCache(getVertexProgram()->getName(), getVertexProgram()->getGLProgramHandle());
            }
            if(mFragmentProgram && mFragmentProgram->isLinked())
            {
                OGRE_CHECK_GL_ERROR(glUseProgramStagesEXT(mGLProgramHandle, GL_FRAGMENT_SHADER_BIT_EXT, mFragmentProgram->getGLProgramHandle()));
                _writeToCache(mFragmentProgram->getName(), mFragmentProgram->getGLProgramHandle());
            }

            // Validate pipeline
            GLSLES::logObjectInfo( getCombinedName() + String("GLSL program pipeline result : "), mGLProgramHandle );
            if(getVertexProgram() && mFragmentProgram && Root::getSingleton().getRenderSystem()->getCapabilities()->hasCapability(RSC_DEBUG))
            {
                glLabelObjectEXT(GL_PROGRAM_PIPELINE_OBJECT_EXT, mGLProgramHandle, 0,
                             (getVertexProgram()->getName() + "/" + mFragmentProgram->getName()).c_str());
            }
        }
    }

#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID || OGRE_PLATFORM == OGRE_PLATFORM_EMSCRIPTEN
    void GLSLESProgramPipeline::notifyOnContextLost()
    {
        OGRE_CHECK_GL_ERROR(glDeleteProgramPipelinesEXT(1, &mGLProgramHandle));
        mGLProgramHandle = 0;
        GLSLESProgramCommon::notifyOnContextLost();
    }
#endif

    //-----------------------------------------------------------------------
    void GLSLESProgramPipeline::activate(void)
    {
        if (!mLinked && !mTriedToLinkAndFailed)
        {
            glGetError(); // Clean up the error. Otherwise will flood log.
            
#if !OGRE_NO_GLES2_GLSL_OPTIMISER
            // Check CmdParams for each shader type to see if we should optimise
            if(mVertexProgram)
            {
                String paramStr = mVertexProgram->getGLSLProgram()->getParameter("use_optimiser");
                if((paramStr == "true") || paramStr.empty())
                {
                    GLSLESProgramPipelineManager::getSingleton().optimiseShaderSource(mVertexProgram);
                }
            }

            if(mFragmentProgram)
            {
                String paramStr = mFragmentProgram->getGLSLProgram()->getParameter("use_optimiser");
                if((paramStr == "true") || paramStr.empty())
                {
                    GLSLESProgramPipelineManager::getSingleton().optimiseShaderSource(mFragmentProgram);
                }
            }
#endif
            compileAndLink();

            extractLayoutQualifiers();

            buildGLUniformReferences();
        }

        if (mLinked)
        {
            OGRE_CHECK_GL_ERROR(glBindProgramPipelineEXT(mGLProgramHandle));
        }
    }

    //-----------------------------------------------------------------------
    void GLSLESProgramPipeline::buildGLUniformReferences(void)
    {
        if (!mUniformRefsBuilt)
        {
            const GpuConstantDefinitionMap* vertParams = 0;
            const GpuConstantDefinitionMap* fragParams = 0;
            if (getVertexProgram())
            {
                vertParams = &(getVertexProgram()->getConstantDefinitions().map);
                GLSLESProgramManager::extractUniforms(getVertexProgram()->getGLProgramHandle(),
                                                      vertParams, NULL, mGLUniformReferences,
                                                      mGLUniformBufferReferences);
            }
            if (mFragmentProgram)
            {
                fragParams = &(mFragmentProgram->getConstantDefinitions().map);
                GLSLESProgramManager::extractUniforms(mFragmentProgram->getGLProgramHandle(), NULL,
                                                      fragParams, mGLUniformReferences,
                                                      mGLUniformBufferReferences);
            }

            mUniformRefsBuilt = true;
        }
    }

    //-----------------------------------------------------------------------
    void GLSLESProgramPipeline::updateUniforms(GpuProgramParametersSharedPtr params, 
                                           uint16 mask, GpuProgramType fromProgType)
    {
        // Iterate through uniform reference list and update uniform values
        GLUniformReferenceIterator currentUniform = mGLUniformReferences.begin();
        GLUniformReferenceIterator endUniform = mGLUniformReferences.end();
        GLuint progID = 0;
        GLUniformCache* uniformCache=0;
        if(fromProgType == GPT_VERTEX_PROGRAM)
        {
            progID = getVertexProgram()->getGLProgramHandle();
            uniformCache = getVertexProgram()->getUniformCache();
        }
        else if(fromProgType == GPT_FRAGMENT_PROGRAM)
        {
            progID = mFragmentProgram->getGLProgramHandle();
            uniformCache = mFragmentProgram->getUniformCache();
        }

        for (;currentUniform != endUniform; ++currentUniform)
        {
            // Only pull values from buffer it's supposed to be in (vertex or fragment)
            // This method will be called twice, once for vertex program params, 
            // and once for fragment program params.
            if (fromProgType == currentUniform->mSourceProgType)
            {
                const GpuConstantDefinition* def = currentUniform->mConstantDef;
                if (def->variability & mask)
                {
                    GLsizei glArraySize = (GLsizei)def->arraySize;
                    bool shouldUpdate = true;
                    switch (def->constType)
                    {
                        case GCT_INT1:
                        case GCT_INT2:
                        case GCT_INT3:
                        case GCT_INT4:
                        case GCT_SAMPLER1D:
                        case GCT_SAMPLER1DSHADOW:
                        case GCT_SAMPLER2D:
                        case GCT_SAMPLER2DSHADOW:
                        case GCT_SAMPLER3D:
                        case GCT_SAMPLERCUBE:
#if OGRE_NO_GLES3_SUPPORT == 0
                        case GCT_SAMPLER2DARRAY:
#endif
                            shouldUpdate = uniformCache->updateUniform(currentUniform->mLocation,
                                                                        params->getIntPointer(def->physicalIndex),
                                                                        static_cast<GLsizei>(def->elementSize * def->arraySize * sizeof(int)));
                            break;
                        default:
                            shouldUpdate = uniformCache->updateUniform(currentUniform->mLocation,
                                                                        params->getFloatPointer(def->physicalIndex),
                                                                        static_cast<GLsizei>(def->elementSize * def->arraySize * sizeof(float)));
                            break;
                    }

                    if(!shouldUpdate)
                        continue;

                    // Get the index in the parameter real list
                    switch (def->constType)
                    {
                        case GCT_FLOAT1:
                            OGRE_CHECK_GL_ERROR(glProgramUniform1fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_FLOAT2:
                            OGRE_CHECK_GL_ERROR(glProgramUniform2fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_FLOAT3:
                            OGRE_CHECK_GL_ERROR(glProgramUniform3fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_FLOAT4:
                            OGRE_CHECK_GL_ERROR(glProgramUniform4fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_2X2:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix2fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                             GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_3X3:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix3fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                             GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_4X4:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix4fvEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                             GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_INT1:
                            OGRE_CHECK_GL_ERROR(glProgramUniform1ivEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getIntPointer(def->physicalIndex)));
                            break;
                        case GCT_INT2:
                            OGRE_CHECK_GL_ERROR(glProgramUniform2ivEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getIntPointer(def->physicalIndex)));
                            break;
                        case GCT_INT3:
                            OGRE_CHECK_GL_ERROR(glProgramUniform3ivEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getIntPointer(def->physicalIndex)));
                            break;
                        case GCT_INT4:
                            OGRE_CHECK_GL_ERROR(glProgramUniform4ivEXT(progID, currentUniform->mLocation, glArraySize, 
                                                                       params->getIntPointer(def->physicalIndex)));
                            break;
                        case GCT_SAMPLER1D:
                        case GCT_SAMPLER1DSHADOW:
                        case GCT_SAMPLER2D:
                        case GCT_SAMPLER2DSHADOW:
                        case GCT_SAMPLER3D:
                        case GCT_SAMPLERCUBE:
#if OGRE_NO_GLES3_SUPPORT == 0
                        case GCT_SAMPLER2DARRAY:
#endif
                            // Samplers handled like 1-element ints
                            OGRE_CHECK_GL_ERROR(glProgramUniform1ivEXT(progID, currentUniform->mLocation, 1,
                                                                       params->getIntPointer(def->physicalIndex)));
                            break;
#if OGRE_NO_GLES3_SUPPORT == 0
                        case GCT_MATRIX_2X3:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix2x3fvEXT(progID, currentUniform->mLocation, glArraySize,
                                                                            GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_2X4:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix2x4fvEXT(progID, currentUniform->mLocation, glArraySize,
                                                                            GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_3X2:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix3x2fvEXT(progID, currentUniform->mLocation, glArraySize,
                                                                            GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_3X4:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix3x4fvEXT(progID, currentUniform->mLocation, glArraySize,
                                                                            GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_4X2:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix4x2fvEXT(progID, currentUniform->mLocation, glArraySize,
                                                                            GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
                        case GCT_MATRIX_4X3:
                            OGRE_CHECK_GL_ERROR(glProgramUniformMatrix4x3fvEXT(progID, currentUniform->mLocation, glArraySize,
                                                                            GL_FALSE, params->getFloatPointer(def->physicalIndex)));
                            break;
#else
                        case GCT_MATRIX_2X3:
                        case GCT_MATRIX_2X4:
                        case GCT_MATRIX_3X2:
                        case GCT_MATRIX_3X4:
                        case GCT_MATRIX_4X2:
                        case GCT_MATRIX_4X3:
                        case GCT_SAMPLER2DARRAY:
#endif
                        case GCT_UNKNOWN:
                        case GCT_SUBROUTINE:
                        case GCT_DOUBLE1:
                        case GCT_DOUBLE2:
                        case GCT_DOUBLE3:
                        case GCT_DOUBLE4:
                        case GCT_SAMPLERRECT:
                        case GCT_MATRIX_DOUBLE_2X2:
                        case GCT_MATRIX_DOUBLE_2X3:
                        case GCT_MATRIX_DOUBLE_2X4:
                        case GCT_MATRIX_DOUBLE_3X2:
                        case GCT_MATRIX_DOUBLE_3X3:
                        case GCT_MATRIX_DOUBLE_3X4:
                        case GCT_MATRIX_DOUBLE_4X2:
                        case GCT_MATRIX_DOUBLE_4X3:
                        case GCT_MATRIX_DOUBLE_4X4:
                        default:
                            break;
                            
                    } // End switch
                } // Variability & mask
            } // fromProgType == currentUniform->mSourceProgType
            
        } // End for
    }
    //-----------------------------------------------------------------------
    void GLSLESProgramPipeline::updateUniformBlocks(GpuProgramParametersSharedPtr params,
                                                  uint16 mask, GpuProgramType fromProgType)
    {
#if OGRE_NO_GLES3_SUPPORT == 0
        // Iterate through the list of uniform buffers and update them as needed
        GLUniformBufferIterator currentBuffer = mGLUniformBufferReferences.begin();
        GLUniformBufferIterator endBuffer = mGLUniformBufferReferences.end();

        const GpuProgramParameters::GpuSharedParamUsageList& sharedParams = params->getSharedParameters();

        GpuProgramParameters::GpuSharedParamUsageList::const_iterator it, end = sharedParams.end();
        for (it = sharedParams.begin(); it != end; ++it)
        {
            for (;currentBuffer != endBuffer; ++currentBuffer)
            {
                GLES2HardwareUniformBuffer* hwGlBuffer = static_cast<GLES2HardwareUniformBuffer*>(currentBuffer->get());
                GpuSharedParametersPtr paramsPtr = it->getSharedParams();

                // Block name is stored in mSharedParams->mName of GpuSharedParamUsageList items
                GLint UniformTransform;
                OGRE_CHECK_GL_ERROR(UniformTransform = glGetUniformBlockIndex(mGLProgramHandle, it->getName().c_str()));
                OGRE_CHECK_GL_ERROR(glUniformBlockBinding(mGLProgramHandle, UniformTransform, hwGlBuffer->getGLBufferBinding()));

                hwGlBuffer->writeData(0, hwGlBuffer->getSizeInBytes(), &paramsPtr->getFloatConstantList().front());
            }
        }
#endif
    }
    //-----------------------------------------------------------------------
    void GLSLESProgramPipeline::updatePassIterationUniforms(GpuProgramParametersSharedPtr params)
    {
        if (params->hasPassIterationNumber())
        {
            size_t index = params->getPassIterationNumberIndex();
            
            GLUniformReferenceIterator currentUniform = mGLUniformReferences.begin();
            GLUniformReferenceIterator endUniform = mGLUniformReferences.end();
            
            // Need to find the uniform that matches the multi pass entry
            for (;currentUniform != endUniform; ++currentUniform)
            {
                // Get the index in the parameter real list
                if (index == currentUniform->mConstantDef->physicalIndex)
                {
                    GLuint progID = 0;
                    if (getVertexProgram() && currentUniform->mSourceProgType == GPT_VERTEX_PROGRAM)
                    {
                        progID = getVertexProgram()->getGLProgramHandle();
                        OGRE_CHECK_GL_ERROR(glProgramUniform1fvEXT(progID, currentUniform->mLocation, 1, params->getFloatPointer(index)));
                    }
                    
                    if (mFragmentProgram && currentUniform->mSourceProgType == GPT_FRAGMENT_PROGRAM)
                    {
                        progID = mFragmentProgram->getGLProgramHandle();
                        OGRE_CHECK_GL_ERROR(glProgramUniform1fvEXT(progID, currentUniform->mLocation, 1, params->getFloatPointer(index)));
                    }

                    // There will only be one multipass entry
                    return;
                }
            }
        }
    }
}
